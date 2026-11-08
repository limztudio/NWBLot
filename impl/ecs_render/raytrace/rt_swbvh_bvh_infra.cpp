// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/software_bvh_builder.h>
#include <impl/ecs_render/raytrace/ray_trace_material_resources.h>
#include <impl/ecs_render/raytrace/rt_swbvh_helpers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SoftwareBvhBuilder::releaseSwBvhScratchHeapHandles(){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(heap.isInitialized()){
        RayTracingDetail::RetireHeapHandle(heap, m_state.m_bvhSortKeysHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_state.m_bvhSortPayloadHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, m_state.m_bvhVisitCounterHeapHandle);
        return;
    }
    m_state.m_bvhSortKeysHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_state.m_bvhSortPayloadHeapHandle = Core::GpuDescriptorHandle::Invalid();
    m_state.m_bvhVisitCounterHeapHandle = Core::GpuDescriptorHandle::Invalid();
}

b8 SoftwareBvhBuilder::ensureBvhSortPipeline(){
    if(m_state.m_bvhSortPipeline)
        return true;
    if(m_state.m_bvhSortPipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software BVH sort requires the initialized global descriptor heap"));
        m_state.m_bvhSortPipelineFailed = true;
        return false;
    }

    // Push-only layout; sort resources use the global heap.
    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(BvhSortPushConstants), Core::ShaderType::Compute, m_state.m_bvhSortBindingLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort binding layout"));
        m_state.m_bvhSortPipelineFailed = true;
        return false;
    }

    if(!m_shaderSystem.loadShader<ComputeShader>(&BvhSubsystem::bitonicSortCs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_BvhBitonicSort", m_state.m_bvhSortShader)){
        m_state.m_bvhSortPipelineFailed = true;
        return false;
    }

    if(!RendererComputePipelineDetail::CreateHeapComputePipeline(device, m_state.m_bvhSortShader, m_state.m_bvhSortBindingLayout, false, m_state.m_bvhSortPipeline)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort compute pipeline"));
        m_state.m_bvhSortPipelineFailed = true;
        return false;
    }
    return true;
}

b8 SoftwareBvhBuilder::ensureBvhSortBuffers(usize paddedCount){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;

    const auto sortHandlesReady = [this]()noexcept{
        return RayTracingDetail::IsHeapHandle(m_state.m_bvhSortKeysHeapHandle, Core::GpuDescriptorClass::StorageBuffer) && RayTracingDetail::IsHeapHandle(m_state.m_bvhSortPayloadHeapHandle, Core::GpuDescriptorClass::StorageBuffer);
    };

    if(m_state.m_bvhSortKeysBuffer && m_state.m_bvhSortPayloadBuffer && m_state.m_bvhSortCapacity >= paddedCount){
        if(sortHandlesReady())
            return true;

        // Register only missing handles to preserve live generations.
        Core::GpuDescriptorHandle acquiredKeys = Core::GpuDescriptorHandle::Invalid();
        Core::GpuDescriptorHandle acquiredPayload = Core::GpuDescriptorHandle::Invalid();
        const auto acquireDescriptors = [&]() -> Expected<void>{
            if(!m_state.m_bvhSortKeysHeapHandle.valid()){
                const auto acquiredKeysResult = RayTracingDetail::RegisterHeapBuffer(heap, *m_state.m_bvhSortKeysBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true);
                if(!acquiredKeysResult)
                    return MakeUnexpected(Failure{});
                acquiredKeys = *acquiredKeysResult;
            }
            if(!m_state.m_bvhSortPayloadHeapHandle.valid()){
                const auto acquiredPayloadResult = RayTracingDetail::RegisterHeapBuffer(heap, *m_state.m_bvhSortPayloadBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true);
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
            m_state.m_bvhSortKeysHeapHandle = acquiredKeys;
        if(acquiredPayload.valid())
            m_state.m_bvhSortPayloadHeapHandle = acquiredPayload;
        return sortHandlesReady();
    }

    const usize capacity = ::NextGrowingCapacity(m_state.m_bvhSortCapacity, paddedCount, s_BvhSortInitialCapacity);

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

    RayTracingDetail::RetireHeapHandle(heap, m_state.m_bvhSortKeysHeapHandle);
    RayTracingDetail::RetireHeapHandle(heap, m_state.m_bvhSortPayloadHeapHandle);
    m_state.m_bvhSortKeysBuffer = Move(keysBuffer);
    m_state.m_bvhSortPayloadBuffer = Move(payloadBuffer);
    m_state.m_bvhSortKeysHeapHandle = keysHeapHandle;
    m_state.m_bvhSortPayloadHeapHandle = payloadHeapHandle;
    m_state.m_bvhSortCapacity = capacity;
    return true;
}

b8 SoftwareBvhBuilder::bvhBitonicSort(Core::CommandList& commandList, u32 elementCount, u32 paddedCount){
    NWB_ASSERT(m_state.m_bvhSortPipeline);
    NWB_ASSERT(m_state.m_bvhSortKeysBuffer);
    NWB_ASSERT(m_state.m_bvhSortPayloadBuffer);
    NWB_ASSERT(RayTracingDetail::IsHeapHandle(m_state.m_bvhSortKeysHeapHandle, Core::GpuDescriptorClass::StorageBuffer));
    NWB_ASSERT(RayTracingDetail::IsHeapHandle(m_state.m_bvhSortPayloadHeapHandle, Core::GpuDescriptorClass::StorageBuffer));

    // Padded count is power-of-two and group-aligned.
    if(paddedCount < static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE))
        return false;

    const NotNull<Core::Buffer*> keysBuffer(m_state.m_bvhSortKeysBuffer.get());
    const NotNull<Core::Buffer*> payloadBuffer(m_state.m_bvhSortPayloadBuffer.get());

    // Sort scratch fits the max mesh. Fence only this dispatch's padded lanes, sentinels included.
    const Core::BufferRange sortRange(0u, static_cast<u64>(paddedCount) * sizeof(u32));
    commandList.setEnableUavBarriersForBuffer(keysBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(payloadBuffer.get(), true);
    commandList.setBufferState(keysBuffer.get(), Core::ResourceStates::UnorderedAccess, false, sortRange);
    commandList.setBufferState(payloadBuffer.get(), Core::ResourceStates::UnorderedAccess, false, sortRange);
    commandList.commitBarriers();

    const u32 groupCount = paddedCount / static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE);
    const u32 keysHeapSlot = m_state.m_bvhSortKeysHeapHandle.slot();
    const u32 payloadHeapSlot = m_state.m_bvhSortPayloadHeapHandle.slot();

    const auto dispatchSort = [this, &commandList](const BvhSortPushConstants& pushConstants, const u32 groups){
        Core::ComputeState computeState;
        computeState.setPipeline(m_state.m_bvhSortPipeline.get());
        commandList.setComputeState(computeState);
        m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *m_state.m_bvhSortPipeline.get());
        commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        commandList.dispatch(groups, 1u, 1u);
    };
    const auto bvhSortBarrier = [&commandList, keysBuffer, payloadBuffer, sortRange](){
        commandList.setBufferState(keysBuffer.get(), Core::ResourceStates::UnorderedAccess, false, sortRange);
        commandList.setBufferState(payloadBuffer.get(), Core::ResourceStates::UnorderedAccess, false, sortRange);
        commandList.commitBarriers();
    };

    {
        const BvhSortPushConstants pushConstants = MakeBvhSortPushConstants(elementCount, 0u, 0u, NWB_BVH_SORT_MODE_LOCAL_TILE, keysHeapSlot, payloadHeapSlot);
        dispatchSort(pushConstants, groupCount);
        bvhSortBarrier();
    }

    // Merge inter-tile globally and intra-tile in groupshared tails.
    for(u32 sequenceSize = static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE) << 1u; sequenceSize <= paddedCount; sequenceSize <<= 1u){
        for(u32 compareDistance = sequenceSize >> 1u; compareDistance >= static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE); compareDistance >>= 1u){
            const BvhSortPushConstants pushConstants = MakeBvhSortPushConstants(elementCount, compareDistance, sequenceSize, NWB_BVH_SORT_MODE_GLOBAL, keysHeapSlot, payloadHeapSlot);
            dispatchSort(pushConstants, groupCount);
            bvhSortBarrier();
        }

        const BvhSortPushConstants tailPushConstants = MakeBvhSortPushConstants(elementCount, 0u, sequenceSize, NWB_BVH_SORT_MODE_GLOBAL_TAIL, keysHeapSlot, payloadHeapSlot);
        dispatchSort(tailPushConstants, groupCount);
        bvhSortBarrier();
    }
    return true;
}

b8 SoftwareBvhBuilder::ensureBvhBuildPipeline(){
    if(m_state.m_bvhMortonPipeline && m_state.m_bvhTopologyPipeline && m_state.m_bvhFitPipeline)
        return true;
    if(m_state.m_bvhBuildPipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software BVH build requires the initialized global descriptor heap"));
        m_state.m_bvhBuildPipelineFailed = true;
        return false;
    }

    // Push-only layout; build resources use the global heap.
    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(BvhBuildPushConstants), Core::ShaderType::Compute, m_state.m_bvhBuildBindingLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH build binding layout"));
        m_state.m_bvhBuildPipelineFailed = true;
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
            m_state.m_bvhBuildBindingLayout,
            shader,
            pipeline
        );
    };

    if(
        !createBuildPipeline(m_state.m_bvhMortonShader, m_state.m_bvhMortonPipeline, &BvhSubsystem::mortonCs, "ECSRender_BvhMorton")
        || !createBuildPipeline(m_state.m_bvhTopologyShader, m_state.m_bvhTopologyPipeline, &BvhSubsystem::topologyCs, "ECSRender_BvhTopology")
        || !createBuildPipeline(m_state.m_bvhFitShader, m_state.m_bvhFitPipeline, &BvhSubsystem::fitCs, "ECSRender_BvhFit")
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH build compute pipeline"));
        m_state.m_bvhBuildPipelineFailed = true;
        return false;
    }
    return true;
}

b8 SoftwareBvhBuilder::ensureBvhVisitCounterBuffer(usize primitiveCount){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;

    if(m_state.m_bvhVisitCounterBuffer && m_state.m_bvhBuildCapacity >= primitiveCount){
        if(RayTracingDetail::IsHeapHandle(m_state.m_bvhVisitCounterHeapHandle, Core::GpuDescriptorClass::StorageBuffer))
            return true;

        if(!m_state.m_bvhVisitCounterHeapHandle.valid()){
            const auto acquired = RayTracingDetail::RegisterHeapBuffer(heap, *m_state.m_bvhVisitCounterBuffer.get(), Core::GpuDescriptorClass::StorageBuffer, true);
            if(acquired){
                m_state.m_bvhVisitCounterHeapHandle = *acquired;
                return true;
            }
        }
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register existing BVH visit counter in the descriptor heap"));
        return false;
    }

    const usize capacity = ::NextGrowingCapacity(m_state.m_bvhBuildCapacity, primitiveCount, s_BvhBuildInitialCapacity);

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

    RayTracingDetail::RetireHeapHandle(heap, m_state.m_bvhVisitCounterHeapHandle);
    m_state.m_bvhVisitCounterBuffer = Move(counterBuffer);
    m_state.m_bvhVisitCounterHeapHandle = *counterHeapHandle;
    m_state.m_bvhBuildCapacity = capacity;
    return true;
}

b8 SoftwareBvhBuilder::ensureSceneBvhBuffers(u32 instanceCount, RayTraceMaterialResources& materials){
    // Binary scene BVH keeps CPU topology; runtime scenes refit its bounds from live mesh roots.
    const usize requiredNodes = static_cast<usize>(instanceCount) * 2u - 1u;
    if(!m_state.m_sceneBvhNodeBuffer || m_state.m_sceneBvhNodeCapacity < requiredNodes){
        const usize capacity = ::NextGrowingCapacity(m_state.m_sceneBvhNodeCapacity, requiredNodes, s_SceneBvhInitialInstanceCapacity * 2u - 1u);

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
        if(!materials.replaceRayTraceMaterialContextHeapHandle(*nodeBuffer.get(), m_state.m_sceneBvhNodeHeapHandle))
            return false;
        m_state.m_sceneBvhNodeBuffer = Move(nodeBuffer);
        m_state.m_sceneBvhNodeCapacity = capacity;
    }

    if(!m_state.m_sceneInstanceBuffer || m_state.m_sceneInstanceCapacity < instanceCount){
        const usize capacity = ::NextGrowingCapacity(m_state.m_sceneInstanceCapacity, instanceCount, s_SceneBvhInitialInstanceCapacity);

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
        if(!materials.replaceRayTraceMaterialContextHeapHandle(*instanceBuffer.get(), m_state.m_sceneInstanceHeapHandle))
            return false;
        m_state.m_sceneInstanceBuffer = Move(instanceBuffer);
        m_state.m_sceneInstanceCapacity = capacity;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created software scene BVH buffers (capacity {} instances)"), static_cast<u64>(capacity));
    }
    return
        materials.ensureRayTraceMaterialContextHeapHandle(*m_state.m_sceneBvhNodeBuffer.get(), m_state.m_sceneBvhNodeHeapHandle)
        && materials.ensureRayTraceMaterialContextHeapHandle(*m_state.m_sceneInstanceBuffer.get(), m_state.m_sceneInstanceHeapHandle)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

