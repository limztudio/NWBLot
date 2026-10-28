// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

        auto bvhSortBindingLayoutResult = device.createBindingLayout(layoutDesc);
        if(bvhSortBindingLayoutResult)
            m_rayTracingState.m_bvhSortBindingLayout = Move(*bvhSortBindingLayoutResult);
        else
            m_rayTracingState.m_bvhSortBindingLayout.reset();
        if(!bvhSortBindingLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort binding layout"));
            m_rayTracingState.m_bvhSortPipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader<ComputeShader>(m_rayTracingState.m_bvhSortShader, &BvhSubsystem::bitonicSortCs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_BvhBitonicSort")){
        m_rayTracingState.m_bvhSortPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(m_rayTracingState.m_bvhSortShader).addBindingLayout(m_rayTracingState.m_bvhSortBindingLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
    auto bvhSortPipelineResult = device.createComputePipeline(pipelineDesc);
    if(bvhSortPipelineResult)
        m_rayTracingState.m_bvhSortPipeline = Move(*bvhSortPipelineResult);
    else
        m_rayTracingState.m_bvhSortPipeline.reset();
    if(!bvhSortPipelineResult){
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

    const auto sortHandlesReady = [this]()noexcept{
        return RayTracingSoftwareBvhDetail::IsStorageBufferHeapHandle(m_rayTracingState.m_bvhSortKeysHeapHandle) && RayTracingSoftwareBvhDetail::IsStorageBufferHeapHandle(m_rayTracingState.m_bvhSortPayloadHeapHandle);
    };

    if(m_rayTracingState.m_bvhSortKeysBuffer && m_rayTracingState.m_bvhSortPayloadBuffer && m_rayTracingState.m_bvhSortCapacity >= paddedCount){
        if(sortHandlesReady())
            return true;

        // Register only missing handles to preserve live generations.
        Core::GpuDescriptorHandle acquiredKeys = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle acquiredPayload = Core::GpuDescriptorHandle::Invalid();
        const auto acquireDescriptors = [&]() -> Expected<void>{
            if(!m_rayTracingState.m_bvhSortKeysHeapHandle.valid()){
                const auto acquiredKeysResult = RayTracingSoftwareBvhDetail::RegisterWritableBvhBuffer(heap, *m_rayTracingState.m_bvhSortKeysBuffer.get());
                if(!acquiredKeysResult)
                    return MakeUnexpected(Failure{});
                acquiredKeys = *acquiredKeysResult;
            }
            if(!m_rayTracingState.m_bvhSortPayloadHeapHandle.valid()){
                const auto acquiredPayloadResult = RayTracingSoftwareBvhDetail::RegisterWritableBvhBuffer(heap, *m_rayTracingState.m_bvhSortPayloadBuffer.get());
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
        const auto keysHeapHandleResult = RayTracingSoftwareBvhDetail::RegisterWritableBvhBuffer(heap, *keysBuffer.get());
        if(!keysHeapHandleResult)
            return MakeUnexpected(Failure{});
        keysHeapHandle = *keysHeapHandleResult;
        const auto payloadHeapHandleResult = RayTracingSoftwareBvhDetail::RegisterWritableBvhBuffer(heap, *payloadBuffer.get());
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

bool RendererRayTracingSystem::bvhBitonicSort(Core::CommandList& commandList, u32 elementCount, u32 paddedCount){
    NWB_ASSERT(m_rayTracingState.m_bvhSortPipeline);
    NWB_ASSERT(m_rayTracingState.m_bvhSortKeysBuffer);
    NWB_ASSERT(m_rayTracingState.m_bvhSortPayloadBuffer);
    NWB_ASSERT(RayTracingSoftwareBvhDetail::IsStorageBufferHeapHandle(m_rayTracingState.m_bvhSortKeysHeapHandle));
    NWB_ASSERT(RayTracingSoftwareBvhDetail::IsStorageBufferHeapHandle(m_rayTracingState.m_bvhSortPayloadHeapHandle));

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

bool RendererRayTracingSystem::ensureBvhBuildPipeline(){
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

    if(!m_rayTracingState.m_bvhBuildBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        // Push-only layout; build resources use the global heap.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(BvhBuildPushConstants)));

        auto bvhBuildBindingLayoutResult = device.createBindingLayout(layoutDesc);
        if(bvhBuildBindingLayoutResult)
            m_rayTracingState.m_bvhBuildBindingLayout = Move(*bvhBuildBindingLayoutResult);
        else
            m_rayTracingState.m_bvhBuildBindingLayout.reset();
        if(!bvhBuildBindingLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH build binding layout"));
            m_rayTracingState.m_bvhBuildPipelineFailed = true;
            return false;
        }
    }

    const auto createBuildPipeline = [this, &device, &heap](Core::ShaderHandle& shader, Core::ComputePipelineHandle& pipeline, const Core::Assets::AssetRef<ComputeShader> BvhSubsystem::* member, const NameText<char> debugLabel)->bool{
        if(pipeline)
            return true;
        if(!m_shaderSystem.loadShader<ComputeShader>(shader, member, Core::ShaderArchive::s_DefaultVariant, Name(debugLabel)))
            return false;

        Core::ComputePipelineDesc pipelineDesc;
        pipelineDesc.setComputeShader(shader).addBindingLayout(m_rayTracingState.m_bvhBuildBindingLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
        auto pipelineResult = device.createComputePipeline(pipelineDesc);
        if(pipelineResult)
            pipeline = Move(*pipelineResult);
        else
            pipeline.reset();
        return static_cast<bool>(pipelineResult);
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

bool RendererRayTracingSystem::ensureBvhVisitCounterBuffer(usize primitiveCount){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;

    if(m_rayTracingState.m_bvhVisitCounterBuffer && m_rayTracingState.m_bvhBuildCapacity >= primitiveCount){
        if(RayTracingSoftwareBvhDetail::IsStorageBufferHeapHandle(m_rayTracingState.m_bvhVisitCounterHeapHandle))
            return true;

        if(!m_rayTracingState.m_bvhVisitCounterHeapHandle.valid()){
            const auto acquired = RayTracingSoftwareBvhDetail::RegisterWritableBvhBuffer(heap, *m_rayTracingState.m_bvhVisitCounterBuffer.get());
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

    const auto counterHeapHandle = RayTracingSoftwareBvhDetail::RegisterWritableBvhBuffer(heap, *counterBuffer.get());
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

bool RendererRayTracingSystem::ensureSceneBvhBuffers(u32 instanceCount){
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

