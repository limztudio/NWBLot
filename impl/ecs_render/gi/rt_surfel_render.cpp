// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/gi/quality_settings.adhd.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::renderSurfelGiPhases(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const b8 dispatchAgeFree,
    const b8 dispatchHashBuild,
    const b8 dispatchSpawn,
    const b8 dispatchTraceBuildArgs,
    const b8 dispatchTrace,
    const b8 dispatchResolve,
    const b8 dispatchRemaining
){
    if(!hasSurfelWork())
        return true;

    NWB_ASSERT(targets.bindless.valid());
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(!heap.isInitialized())
        return false;

    // Only the trace pass is backend-specific.
    const b8 useHwTrace = m_rayTracingState.m_surfelUseHwTrace;
    const b8 useCsgTrace = m_rayTracingState.m_surfelUseCsgTrace;
    Core::ComputePipeline* const tracePipeline = useHwTrace
        ? (useCsgTrace ? m_rayTracingState.m_surfelTraceCsgHwPipeline.get() : m_rayTracingState.m_surfelTraceHwPipeline.get())
        : (useCsgTrace ? m_rayTracingState.m_surfelTraceCsgPipeline.get() : m_rayTracingState.m_surfelTracePipeline.get())
    ;

    if(
        !m_rayTracingState.m_surfelSpawnPipeline
        || !m_rayTracingState.m_surfelAgeFreePipeline
        || !m_rayTracingState.m_surfelHashBuildPipeline
        || !tracePipeline
        || !m_rayTracingState.m_surfelResolvePipeline
        || !m_rayTracingState.m_surfelUpsamplePipeline
        || !m_rayTracingState.m_surfelTraceBuildArgsPipeline
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: active surfel GI phase pipeline is missing"));
        return false;
    }

    if(
        !targets.bindless.valid()
        || (targets.surfelResolveFactor != static_cast<u32>(SurfelGiResolveResolution::Half) && targets.surfelResolveFactor != static_cast<u32>(SurfelGiResolveResolution::Quarter))
        || !deferredLightingResources.valid()
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelConstantsHeapHandle, Core::GpuDescriptorClass::UniformBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelPoolHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelGuidePoolHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelCellHeadHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelCounterHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelTraceIndirectArgsHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelFreeListHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelPoolSnapshotHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelCellHeadSnapshotHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle, Core::GpuDescriptorClass::UniformBuffer)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.slotsBufferDescriptor, Core::GpuDescriptorClass::UniformBuffer)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.gbufferWorldPosition, Core::GpuDescriptorClass::SampledImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.gbufferNormal, Core::GpuDescriptorClass::SampledImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.surfelIrradianceHalf, Core::GpuDescriptorClass::SampledImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.surfelIrradianceHalfStorage, Core::GpuDescriptorClass::StorageImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.surfelIrradianceStorage, Core::GpuDescriptorClass::StorageImage)
        || (useHwTrace && (!m_rayTracingState.m_tlas || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_tlasHeapHandle, Core::GpuDescriptorClass::AccelStruct)))
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel GI heap registration is incomplete"));
        return false;
    }

    const u32 poolCapacity = m_rayTracingState.m_surfelPoolCapacity;

    SurfelHeapPushConstants surfelPush = MakeSurfelHeapPushConstants(
        m_rayTracingState.m_surfelConstantsHeapHandle.slot(),
        m_rayTracingState.m_surfelPoolHeapHandle.slot(),
        m_rayTracingState.m_surfelGuidePoolHeapHandle.slot(),
        m_rayTracingState.m_surfelCellHeadHeapHandle.slot(),
        m_rayTracingState.m_surfelCounterHeapHandle.slot(),
        m_rayTracingState.m_surfelFreeListHeapHandle.slot(),
        m_rayTracingState.m_surfelPoolSnapshotHeapHandle.slot(),
        m_rayTracingState.m_surfelCellHeadSnapshotHeapHandle.slot(),
        m_rayTracingState.m_surfelTraceIndirectArgsHeapHandle.slot(),
        targets.bindless.slotsBufferDescriptor.slot(),
        m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.slot()
    );

    // Order every in-place field update, including prior-frame spawn writes.
    commandList.setEnableUavBarriersForBuffer(m_rayTracingState.m_surfelPoolBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(m_rayTracingState.m_surfelGuidePoolBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(m_rayTracingState.m_surfelCellHeadBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(m_rayTracingState.m_surfelCounterBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(m_rayTracingState.m_surfelFreeListBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(m_rayTracingState.m_surfelTraceIndirectArgsBuffer.get(), true);

    // Age-free recycles unseen surfels before the graph-owned cell-head reset and hash rebuild.
    if(dispatchAgeFree){
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_SurfelAgeFree, m_graphics.getDevice(), commandList);

        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(m_rayTracingState.m_surfelAgeFreePipeline.get());
        commandList.setComputeState(state);
        heap.bindCompute(commandList, *m_rayTracingState.m_surfelAgeFreePipeline.get());
        commandList.setPushConstants(&surfelPush, sizeof(surfelPush));
        commandList.dispatch(DivideUp(poolCapacity, static_cast<u32>(NWB_SURFEL_LINEAR_GROUP_SIZE)), 1u, 1u);
    }

    if(!dispatchHashBuild && !dispatchSpawn && !dispatchTraceBuildArgs && !dispatchTrace && !dispatchResolve && !dispatchRemaining)
        return true;

    // Rebuild bucket lists before spawning uncovered surface representatives.
    if(dispatchHashBuild){
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_SurfelHashBuild, m_graphics.getDevice(), commandList);

        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(m_rayTracingState.m_surfelHashBuildPipeline.get());
        commandList.setComputeState(state);
        heap.bindCompute(commandList, *m_rayTracingState.m_surfelHashBuildPipeline.get());
        commandList.setPushConstants(&surfelPush, sizeof(surfelPush));
        commandList.dispatch(DivideUp(poolCapacity, static_cast<u32>(NWB_SURFEL_LINEAR_GROUP_SIZE)), 1u, 1u);
    }

    if(!dispatchSpawn && !dispatchTraceBuildArgs && !dispatchTrace && !dispatchResolve && !dispatchRemaining)
        return true;

    // Spawn retains compatible surfaces and inserts uncovered candidates into bounded bucket lists.
    if(dispatchSpawn){
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_SurfelSpawn, m_graphics.getDevice(), commandList);

        commandList.commitBarriers();

        surfelPush.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
        surfelPush.normalSlot = targets.bindless.gbufferNormal.slot();

        Core::ComputeState state;
        state.setPipeline(m_rayTracingState.m_surfelSpawnPipeline.get());
        commandList.setComputeState(state);
        heap.bindCompute(commandList, *m_rayTracingState.m_surfelSpawnPipeline.get());
        commandList.setPushConstants(&surfelPush, sizeof(surfelPush));
        const u32 tilesX = DivideUp(targets.width, NWB_SURFEL_SPAWN_TILE);
        const u32 tilesY = DivideUp(targets.height, NWB_SURFEL_SPAWN_TILE);
        commandList.dispatch(DivideUp(tilesX, static_cast<u32>(NWB_SURFEL_GROUP_SIZE)), DivideUp(tilesY, static_cast<u32>(NWB_SURFEL_GROUP_SIZE)), 1u);
    }

    if(!dispatchTraceBuildArgs && !dispatchTrace && !dispatchResolve && !dispatchRemaining)
        return true;

    // Build an indirect dispatch sized for live surfels.
    if(dispatchTraceBuildArgs){
        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(m_rayTracingState.m_surfelTraceBuildArgsPipeline.get());
        commandList.setComputeState(state);
        heap.bindCompute(commandList, *m_rayTracingState.m_surfelTraceBuildArgsPipeline.get());
        commandList.setPushConstants(&surfelPush, sizeof(surfelPush));
        commandList.dispatch(NWB_SURFEL_TRACE_BUILD_ARGS_DISPATCH_GROUP_COUNT_X, NWB_SURFEL_TRACE_BUILD_ARGS_DISPATCH_GROUP_COUNT_Y, NWB_SURFEL_TRACE_BUILD_ARGS_DISPATCH_GROUP_COUNT_Z);
    }

    if(!dispatchTrace && !dispatchResolve && !dispatchRemaining)
        return true;

    if(dispatchTrace){
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_SurfelTrace, m_graphics.getDevice(), commandList);

        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(tracePipeline);
        state.setIndirectParams(m_rayTracingState.m_surfelTraceIndirectArgsBuffer.get());
        commandList.setComputeState(state);
        // Hardware trace additionally selects the TLAS generation at set 2.
        if(m_rayTracingState.m_surfelUseHwTrace && !m_rayTracingState.m_tlasHeapHandle.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: cannot dispatch surfel HW GI without the descriptor-heap TLAS handle"));
            return false;
        }
        const Core::GpuDescriptorHandle tlasHeapHandle = m_rayTracingState.m_surfelUseHwTrace ? m_rayTracingState.m_tlasHeapHandle : Core::GpuDescriptorHandle::Invalid();
        heap.bindCompute(commandList, *tracePipeline, tlasHeapHandle);
        commandList.setPushConstants(&surfelPush, sizeof(surfelPush));
        commandList.dispatchIndirect(0u);
    }

    if(!dispatchResolve && !dispatchRemaining)
        return true;

    // Resolve at the target generation's selected scale so deferred lighting never touches the writable pool.
    if(dispatchResolve){
        const SurfelGiResolveSize resolveSize = MakeSurfelGiResolveSize(targets.width, targets.height, targets.surfelResolveFactor);
        {
            Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_SurfelResolve, m_graphics.getDevice(), commandList);

            commandList.commitBarriers();

            surfelPush.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
            surfelPush.normalSlot = targets.bindless.gbufferNormal.slot();
            surfelPush.outputStorageHeapSlot = targets.bindless.surfelIrradianceHalfStorage.slot();

            Core::ComputeState state;
            state.setPipeline(m_rayTracingState.m_surfelResolvePipeline.get());
            commandList.setComputeState(state);
            heap.bindCompute(commandList, *m_rayTracingState.m_surfelResolvePipeline.get());
            commandList.setPushConstants(&surfelPush, sizeof(surfelPush));
            const u32 groupSize = static_cast<u32>(NWB_SURFEL_RESOLVE_GROUP_SIZE);
            commandList.dispatch(DivideUp(resolveSize.width, groupSize), DivideUp(resolveSize.height, groupSize), 1u);
            if(commandList.commandRecordingFailed())
                return false;
            LogSurfelGiResolveDispatchIfNeeded(targets.surfelResolveFactor, targets.width, targets.height, resolveSize, m_rayTracingState.m_surfelResolveDispatchLogged);
        }
    }

    if(!dispatchRemaining)
        return true;

    // Surface-aware upsample preserves coverage across edges.
    {
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_SurfelUpsample, m_graphics.getDevice(), commandList);

        commandList.commitBarriers();

        surfelPush.halfIrradianceSlot = targets.bindless.surfelIrradianceHalf.slot();
        surfelPush.normalSlot = targets.bindless.gbufferNormal.slot();
        surfelPush.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
        surfelPush.outputStorageHeapSlot = targets.bindless.surfelIrradianceStorage.slot();

        Core::ComputeState state;
        state.setPipeline(m_rayTracingState.m_surfelUpsamplePipeline.get());
        commandList.setComputeState(state);
        heap.bindCompute(commandList, *m_rayTracingState.m_surfelUpsamplePipeline.get());
        commandList.setPushConstants(&surfelPush, sizeof(surfelPush));
        const u32 groupSize = static_cast<u32>(NWB_SURFEL_UPSAMPLE_GROUP_SIZE);
        commandList.dispatch(DivideUp(targets.width, groupSize), DivideUp(targets.height, groupSize), 1u);
    }

    // The graph-owned late copy publishes its token only after Transfer/Compute/Graphics accepts. This pass only consumes completed diagnostics.
    {
        const u32 frameIndex = m_rayTracingState.m_surfelFrameIndex;
        const Core::QueueSubmissionToken submissionToken = m_rayTracingState.m_surfelCountReadbackSubmissionToken;
        b8 submissionComplete = false;
        if(submissionToken.valid() && submissionToken.hasPhysicalQueueIdentity()){
            const Expected<u64> completedValue = m_graphics.getDevice().queueGetCompletedInstance(Core::GpuPhysicalQueueId{ .index = submissionToken.physicalQueueIndex, .deviceGeneration = submissionToken.deviceGeneration, });
            submissionComplete = completedValue && *completedValue >= submissionToken.value;
        }
        if(submissionComplete && IsSurfelCountReadbackDelayElapsed(frameIndex, m_rayTracingState.m_surfelCountReadbackFrame)){
            Core::Buffer& readback = *m_rayTracingState.m_surfelCounterReadback;
            const Expected<void*> mappedCounts = m_graphics.getDevice().mapBuffer(readback, Core::CpuAccessMode::Read);
            if(mappedCounts){
                const NotNull<const u32*> counts = MakeNotNull(static_cast<const u32*>(*mappedCounts));
                const u32 bumpTop = counts.get()[NWB_SURFEL_COUNTER_BUMP_TOP];
                const u32 freeTop = counts.get()[NWB_SURFEL_COUNTER_FREE_TOP];
                const u32 geometryFailures = counts.get()[NWB_SURFEL_COUNTER_TRACE_GEOMETRY_FAILURE];
                m_graphics.getDevice().unmapBuffer(readback);
                LogSurfelCountReadbackCounts(
                    bumpTop,
                    freeTop,
                    geometryFailures,
                    m_rayTracingState.m_surfelPoolCapacity,
                    m_rayTracingState.m_surfelTraceGeometryFailureFlags
                );
            }
            m_rayTracingState.m_surfelCountReadbackSubmissionToken = {};
        }
    }

    // Subsequent frames use steady-state round-robin updates.
    AdvanceSurfelGiFrameState(m_rayTracingState.m_surfelSeeded, m_rayTracingState.m_surfelFrameIndex);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

