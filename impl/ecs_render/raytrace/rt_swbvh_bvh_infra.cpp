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


bool RendererRayTracingSystem::ensureBvhSortPipeline(){
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

    if(!m_rayTracingState.m_bvhSortBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // Push-only layout; sort resources use the global heap.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(BvhSortPushConstants)));

        m_rayTracingState.m_bvhSortBindingLayout = device.createBindingLayout(layoutDesc);
        if(!m_rayTracingState.m_bvhSortBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort binding layout"));
            m_rayTracingState.m_bvhSortPipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader(
        m_rayTracingState.m_bvhSortShader,
        AssetsGraphicsBvh::s_BitonicSortShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_BvhBitonicSort"
    )){
        m_rayTracingState.m_bvhSortPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(m_rayTracingState.m_bvhSortShader)
        .addBindingLayout(m_rayTracingState.m_bvhSortBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    m_rayTracingState.m_bvhSortPipeline = device.createComputePipeline(pipelineDesc);
    if(!m_rayTracingState.m_bvhSortPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort compute pipeline"));
        m_rayTracingState.m_bvhSortPipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureBvhSortBuffers(usize paddedCount){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;

    const auto sortHandlesReady = [this](){
        return
            __hidden_rt_swbvh::IsStorageBufferHeapHandle(m_rayTracingState.m_bvhSortKeysHeapHandle)
            && __hidden_rt_swbvh::IsStorageBufferHeapHandle(m_rayTracingState.m_bvhSortPayloadHeapHandle)
        ;
    };

    if(
        m_rayTracingState.m_bvhSortKeysBuffer
        && m_rayTracingState.m_bvhSortPayloadBuffer
        && m_rayTracingState.m_bvhSortCapacity >= paddedCount
    ){
        if(sortHandlesReady())
            return true;

        // Register only missing handles to preserve live generations.
        Core::GpuDescriptorHandle acquiredKeys = Core::GpuDescriptorHandle::invalid();
        Core::GpuDescriptorHandle acquiredPayload = Core::GpuDescriptorHandle::invalid();
        if(
            (!m_rayTracingState.m_bvhSortKeysHeapHandle.valid()
                && !__hidden_rt_swbvh::RegisterWritableBvhBuffer(heap, *m_rayTracingState.m_bvhSortKeysBuffer.get(), acquiredKeys))
            || (!m_rayTracingState.m_bvhSortPayloadHeapHandle.valid()
                && !__hidden_rt_swbvh::RegisterWritableBvhBuffer(heap, *m_rayTracingState.m_bvhSortPayloadBuffer.get(), acquiredPayload))
        ){
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

    const usize capacity = ::NextGrowingCapacity(
        m_rayTracingState.m_bvhSortCapacity,
        paddedCount,
        s_BvhSortInitialCapacity
    );

    Core::BufferDesc keysBufferDesc;
    keysBufferDesc
        .setByteSize(static_cast<u64>(sizeof(u32) * capacity))
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name("bvh_sort_keys"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle keysBuffer = m_graphics.createBuffer(keysBufferDesc);
    if(!keysBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort keys buffer"));
        return false;
    }

    Core::BufferDesc payloadBufferDesc;
    payloadBufferDesc
        .setByteSize(static_cast<u64>(sizeof(u32) * capacity))
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name("bvh_sort_payload"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle payloadBuffer = m_graphics.createBuffer(payloadBufferDesc);
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

    RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_bvhSortKeysHeapHandle);
    RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_bvhSortPayloadHeapHandle);
    m_rayTracingState.m_bvhSortKeysBuffer = Move(keysBuffer);
    m_rayTracingState.m_bvhSortPayloadBuffer = Move(payloadBuffer);
    m_rayTracingState.m_bvhSortKeysHeapHandle = keysHeapHandle;
    m_rayTracingState.m_bvhSortPayloadHeapHandle = payloadHeapHandle;
    m_rayTracingState.m_bvhSortCapacity = capacity;
    return true;
}

bool RendererRayTracingSystem::bvhBitonicSort(Core::CommandList& commandList, u32 elementCount, u32 paddedCount){
    NWB_ASSERT(m_rayTracingState.m_bvhSortPipeline);
    NWB_ASSERT(m_rayTracingState.m_bvhSortKeysBuffer);
    NWB_ASSERT(m_rayTracingState.m_bvhSortPayloadBuffer);
    NWB_ASSERT(__hidden_rt_swbvh::IsStorageBufferHeapHandle(m_rayTracingState.m_bvhSortKeysHeapHandle));
    NWB_ASSERT(__hidden_rt_swbvh::IsStorageBufferHeapHandle(m_rayTracingState.m_bvhSortPayloadHeapHandle));

    // Padded count is power-of-two and group-aligned.
    if(paddedCount < static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE))
        return false;

    Core::Buffer* keysBuffer = m_rayTracingState.m_bvhSortKeysBuffer.get();
    Core::Buffer* payloadBuffer = m_rayTracingState.m_bvhSortPayloadBuffer.get();

    // Sort scratch is allocated for the maximum mesh size. Fence only this dispatch's padded lanes, including sentinel entries that participate in the sorting network.
    const Core::BufferRange sortRange(0u, static_cast<u64>(paddedCount) * sizeof(u32));
    commandList.setEnableUavBarriersForBuffer(keysBuffer, true);
    commandList.setEnableUavBarriersForBuffer(payloadBuffer, true);
    commandList.setBufferState(keysBuffer, Core::ResourceStates::UnorderedAccess, false, sortRange);
    commandList.setBufferState(payloadBuffer, Core::ResourceStates::UnorderedAccess, false, sortRange);
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
        commandList.setBufferState(keysBuffer, Core::ResourceStates::UnorderedAccess, false, sortRange);
        commandList.setBufferState(payloadBuffer, Core::ResourceStates::UnorderedAccess, false, sortRange);
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
        m_rayTracingState.m_bvhMortonPipeline
        && m_rayTracingState.m_bvhTopologyPipeline
        && m_rayTracingState.m_bvhFitPipeline
    )
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

    if(!m_rayTracingState.m_bvhBuildBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // Push-only layout; build resources use the global heap.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(BvhBuildPushConstants)));

        m_rayTracingState.m_bvhBuildBindingLayout = device.createBindingLayout(layoutDesc);
        if(!m_rayTracingState.m_bvhBuildBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH build binding layout"));
            m_rayTracingState.m_bvhBuildPipelineFailed = true;
            return false;
        }
    }

    const auto createBuildPipeline = [this, &device, &heap](
        Core::ShaderHandle& shader,
        Core::ComputePipelineHandle& pipeline,
        const Name& shaderName,
        const NotNull<const char*> debugLabel
    )->bool{
        if(pipeline)
            return true;
        if(!m_shaderSystem.loadShader(shader, shaderName, Core::ShaderArchive::s_DefaultVariant, Core::ShaderType::Compute, debugLabel.get()))
            return false;

        Core::ComputePipelineDesc pipelineDesc;
        pipelineDesc
            .setComputeShader(shader)
            .addBindingLayout(m_rayTracingState.m_bvhBuildBindingLayout)
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout())
        ;
        pipeline = device.createComputePipeline(pipelineDesc);
        return pipeline != nullptr;
    };

    if(
        !createBuildPipeline(m_rayTracingState.m_bvhMortonShader, m_rayTracingState.m_bvhMortonPipeline, AssetsGraphicsBvh::s_BvhMortonShaderName, MakeNotNull("ECSRender_BvhMorton"))
        || !createBuildPipeline(m_rayTracingState.m_bvhTopologyShader, m_rayTracingState.m_bvhTopologyPipeline, AssetsGraphicsBvh::s_BvhTopologyShaderName, MakeNotNull("ECSRender_BvhTopology"))
        || !createBuildPipeline(m_rayTracingState.m_bvhFitShader, m_rayTracingState.m_bvhFitPipeline, AssetsGraphicsBvh::s_BvhFitShaderName, MakeNotNull("ECSRender_BvhFit"))
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH build compute pipeline"));
        m_rayTracingState.m_bvhBuildPipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureBvhVisitCounterBuffer(usize primitiveCount){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;

    if(m_rayTracingState.m_bvhVisitCounterBuffer && m_rayTracingState.m_bvhBuildCapacity >= primitiveCount){
        if(__hidden_rt_swbvh::IsStorageBufferHeapHandle(m_rayTracingState.m_bvhVisitCounterHeapHandle))
            return true;

        Core::GpuDescriptorHandle acquired = Core::GpuDescriptorHandle::invalid();
        if(!m_rayTracingState.m_bvhVisitCounterHeapHandle.valid()
            && __hidden_rt_swbvh::RegisterWritableBvhBuffer(heap, *m_rayTracingState.m_bvhVisitCounterBuffer.get(), acquired)){
            m_rayTracingState.m_bvhVisitCounterHeapHandle = acquired;
            return true;
        }
        RayTracingDetail::RetireHeapHandle(heap, acquired);
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register existing BVH visit counter in the descriptor heap"));
        return false;
    }

    const usize capacity = ::NextGrowingCapacity(
        m_rayTracingState.m_bvhBuildCapacity,
        primitiveCount,
        s_BvhBuildInitialCapacity
    );

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
    Core::BufferHandle counterBuffer = m_graphics.createBuffer(counterBufferDesc);
    if(!counterBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH visit counter buffer"));
        return false;
    }

    Core::GpuDescriptorHandle counterHeapHandle = Core::GpuDescriptorHandle::invalid();
    if(!__hidden_rt_swbvh::RegisterWritableBvhBuffer(heap, *counterBuffer.get(), counterHeapHandle)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register BVH visit counter in the descriptor heap"));
        return false;
    }

    RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_bvhVisitCounterHeapHandle);
    m_rayTracingState.m_bvhVisitCounterBuffer = Move(counterBuffer);
    m_rayTracingState.m_bvhVisitCounterHeapHandle = counterHeapHandle;
    m_rayTracingState.m_bvhBuildCapacity = capacity;
    return true;
}

bool RendererRayTracingSystem::createMeshBvhStorage(
    usize primitiveCount,
    Core::BufferHandle& nodeBuffer,
    Core::BufferHandle& parentBuffer,
    Core::GpuDescriptorHandle& nodeHeapHandle,
    Core::GpuDescriptorHandle& parentHeapHandle
){
    auto& device = m_graphics.getDevice();
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
    Core::BufferHandle newNodeBuffer = m_graphics.createBuffer(nodeBufferDesc);
    if(!newNodeBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create per-mesh BVH node buffer"));
        return false;
    }

    Core::BufferDesc parentBufferDesc;
    parentBufferDesc
        .setByteSize(static_cast<u64>(sizeof(u32) * nodeCount))
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name("bvh_mesh_parent"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle newParentBuffer = m_graphics.createBuffer(parentBufferDesc);
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
        && m_rayTracingState.m_bvhSortPipeline
        && m_rayTracingState.m_bvhSortKeysBuffer
        && m_rayTracingState.m_bvhSortPayloadBuffer
        && __hidden_rt_swbvh::IsStorageBufferHeapHandle(m_rayTracingState.m_bvhSortKeysHeapHandle)
        && __hidden_rt_swbvh::IsStorageBufferHeapHandle(m_rayTracingState.m_bvhSortPayloadHeapHandle)
        && m_rayTracingState.m_bvhMortonPipeline
        && m_rayTracingState.m_bvhTopologyPipeline
        && m_rayTracingState.m_bvhFitPipeline
        && m_rayTracingState.m_bvhVisitCounterBuffer
        && __hidden_rt_swbvh::IsStorageBufferHeapHandle(m_rayTracingState.m_bvhVisitCounterHeapHandle)
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

    Core::Buffer* keysBuffer = m_rayTracingState.m_bvhSortKeysBuffer.get();
    Core::Buffer* payloadBuffer = m_rayTracingState.m_bvhSortPayloadBuffer.get();
    Core::Buffer* visitCounterBuffer = m_rayTracingState.m_bvhVisitCounterBuffer.get();
    Core::Buffer* meshNodeBuffer = nodeBuffer.get();
    Core::Buffer* meshParentBuffer = parentBuffer.get();

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
    StoreFloat(VectorSetW(aabbMin, 0.0f), pushConstants.aabbMin);
    StoreFloat(VectorSetW(aabbMax, 0.0f), pushConstants.aabbMax);

    // The graph-split pure-software route lowers these typed CopyDest clears as adjacent built-in tasks. Direct compatibility routes preserve their established native sentinel setup here.
    if(!sentinelClearsGraphOwned){
        commandList.setBufferState(keysBuffer, Core::ResourceStates::CopyDest);
        commandList.setBufferState(meshParentBuffer, Core::ResourceStates::CopyDest);
        commandList.setBufferState(visitCounterBuffer, Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        commandList.clearBufferUInt(*keysBuffer, BvhNodeIndex::Invalid);
        commandList.clearBufferUInt(*meshParentBuffer, BvhNodeIndex::Invalid);
        commandList.clearBufferUInt(*visitCounterBuffer, 0u);
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

    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    const auto dispatchBuildKernel = [&commandList, &pushConstants, &heap](Core::ComputePipeline& pipeline, const u32 groupCount){
        Core::ComputeState computeState;
        computeState.setPipeline(&pipeline);
        commandList.setComputeState(computeState);
        heap.bindCompute(commandList, pipeline);
        commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        commandList.dispatch(groupCount, 1u, 1u);
    };

    // The pure-software graph callback declares every input/output state. It owns the first boundary after its typed clears; direct callers retain the standalone native transition/UAV fence.
    if(!graphBoundaryStatesOwned)
        bvhBuildBarrier();

    dispatchBuildKernel(*m_rayTracingState.m_bvhMortonPipeline, DivideUp(primitiveCount, static_cast<u32>(NWB_BVH_BUILD_GROUP_SIZE)));
    bvhBuildBarrier();

    // Separates rebuild-sort timing from the one-shot self-test.
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
    // Shadow Preparation's declared successor uses lower the final node UAV -> SRV and retained scratch UAV handoffs for graph callers. Keep the direct close fence for compatibility recorders.
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

    Core::Buffer* keysBuffer = m_rayTracingState.m_bvhSortKeysBuffer.get();
    Core::Buffer* payloadBuffer = m_rayTracingState.m_bvhSortPayloadBuffer.get();
    Core::Buffer* meshNodeBuffer = nodeBuffer.get();
    Core::Buffer* meshParentBuffer = parentBuffer.get();
    Core::Buffer* visitCounterBuffer = m_rayTracingState.m_bvhVisitCounterBuffer.get();

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

    // Refit retains topology and recomputes boxes. The pure-software graph route supplies this typed counter clear immediately before the callback;
    // direct routes retain the native compatibility primitive.
    if(!sentinelClearsGraphOwned){
        commandList.setBufferState(visitCounterBuffer, Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        commandList.clearBufferUInt(*visitCounterBuffer, 0u);
    }

    commandList.setEnableUavBarriersForBuffer(keysBuffer, true);
    commandList.setEnableUavBarriersForBuffer(payloadBuffer, true);
    commandList.setEnableUavBarriersForBuffer(meshNodeBuffer, true);
    commandList.setEnableUavBarriersForBuffer(meshParentBuffer, true);
    commandList.setEnableUavBarriersForBuffer(visitCounterBuffer, true);
    // Fit declares all scratch views, so direct callers retain its native entry UAV fence.
    // The graph-split pure-software callback declares these exact states and lowers the CopyDest/UAV handoff in its prologue.
    if(!graphBoundaryStatesOwned){
        commandList.setBufferState(keysBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(payloadBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(meshNodeBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(meshParentBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(visitCounterBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
    }

    Core::ComputeState computeState;
    computeState.setPipeline(m_rayTracingState.m_bvhFitPipeline.get());
    commandList.setComputeState(computeState);
    m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *m_rayTracingState.m_bvhFitPipeline.get());
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.dispatch(DivideUp(primitiveCount, static_cast<u32>(NWB_BVH_BUILD_GROUP_SIZE)), 1u, 1u);

    // The graph successor owns this final node state for pure software; direct callers retain it.
    if(!graphBoundaryStatesOwned){
        commandList.setBufferState(meshNodeBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
    }
    return true;
}

bool RendererRayTracingSystem::updateMeshSwBvh(
    Core::CommandList& commandList,
    ECSRenderDetail::MeshRayTracingResourceSnapshot& meshResources
){
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
    const bool firstBuild = !meshResources.swBvhTopologyBuilt || !meshResources.swBvhBuildAccepted;
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
    // Binary scene BVH keeps CPU topology; runtime scenes refit its bounds from live mesh roots.
    const usize requiredNodes = static_cast<usize>(instanceCount) * 2u - 1u;
    if(!m_rayTracingState.m_sceneBvhNodeBuffer || m_rayTracingState.m_sceneBvhNodeCapacity < requiredNodes){
        const usize capacity = ::NextGrowingCapacity(
            m_rayTracingState.m_sceneBvhNodeCapacity,
            requiredNodes,
            s_SceneBvhInitialInstanceCapacity * 2u - 1u
        );

        Core::BufferDesc nodeBufferDesc;
        nodeBufferDesc
            .setByteSize(static_cast<u64>(sizeof(NwbBvhNodeGpu) * capacity))
            .setStructStride(sizeof(NwbBvhNodeGpu))
            .setCanHaveUAVs(true)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setDebugName(Name("scene_bvh_nodes"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        Core::BufferHandle nodeBuffer = m_graphics.createBuffer(nodeBufferDesc);
        if(!nodeBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene BVH node buffer"));
            return false;
        }
        // Register the new view before replacing ownership to keep the old pair retryable.
        if(!replaceRayTraceMaterialContextHeapHandle(*nodeBuffer.get(), m_rayTracingState.m_sceneBvhNodeHeapHandle))
            return false;
        m_rayTracingState.m_sceneBvhNodeBuffer = Move(nodeBuffer);
        m_rayTracingState.m_sceneBvhNodeCapacity = capacity;
    }

    if(!m_rayTracingState.m_sceneInstanceBuffer || m_rayTracingState.m_sceneInstanceCapacity < instanceCount){
        const usize capacity = ::NextGrowingCapacity(
            m_rayTracingState.m_sceneInstanceCapacity,
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
        Core::BufferHandle instanceBuffer = m_graphics.createBuffer(instanceBufferDesc);
        if(!instanceBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene BVH instance buffer"));
            return false;
        }
        if(!replaceRayTraceMaterialContextHeapHandle(*instanceBuffer.get(), m_rayTracingState.m_sceneInstanceHeapHandle))
            return false;
        m_rayTracingState.m_sceneInstanceBuffer = Move(instanceBuffer);
        m_rayTracingState.m_sceneInstanceCapacity = capacity;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created software scene BVH buffers (capacity {} instances)")
            , static_cast<u64>(capacity)
        );
    }
    return ensureRayTraceMaterialContextHeapHandle(
        *m_rayTracingState.m_sceneBvhNodeBuffer.get(),
        m_rayTracingState.m_sceneBvhNodeHeapHandle
    ) && ensureRayTraceMaterialContextHeapHandle(
        *m_rayTracingState.m_sceneInstanceBuffer.get(),
        m_rayTracingState.m_sceneInstanceHeapHandle
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

