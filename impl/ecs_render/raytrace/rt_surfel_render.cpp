// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <core/task/gpu/compiled_graph.h>
#include <impl/ecs_render/raytrace/rt_surfel_tasks.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::renderSurfelGi(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const bool graphEntryStatesOwned
){
    return renderSurfelGiPhases(
        commandList,
        targets,
        deferredLightingResources,
        graphEntryStatesOwned,
        true,
        true,
        true,
        true,
        true,
        true,
        true,
        false,
        false,
        false,
        false,
        false
    );
}


bool RendererRayTracingSystem::renderSurfelGiAgeFree(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const bool graphEntryStatesOwned
){
    return renderSurfelGiPhases(
        commandList,
        targets,
        deferredLightingResources,
        graphEntryStatesOwned,
        true,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false
    );
}


bool RendererRayTracingSystem::renderSurfelGiAfterAgeFree(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const bool graphEntryStatesOwned,
    const bool graphOwnsCellHeadClear,
    const bool graphOwnsHashBuild,
    const bool graphOwnsSpawn,
    const bool graphOwnsTraceBuildArgs,
    const bool graphOwnsTrace,
    const bool graphOwnsResolve
){
    return renderSurfelGiPhases(
        commandList,
        targets,
        deferredLightingResources,
        graphEntryStatesOwned,
        false,
        !graphOwnsHashBuild,
        !graphOwnsSpawn,
        !graphOwnsTraceBuildArgs,
        !graphOwnsTrace,
        !graphOwnsResolve,
        true,
        graphOwnsCellHeadClear,
        graphOwnsHashBuild,
        graphOwnsTraceBuildArgs,
        graphOwnsTrace,
        graphOwnsResolve
    );
}


bool RendererRayTracingSystem::renderSurfelGiHashBuild(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const bool graphEntryStatesOwned
){
    return renderSurfelGiPhases(
        commandList,
        targets,
        deferredLightingResources,
        graphEntryStatesOwned,
        false,
        true,
        false,
        false,
        false,
        false,
        false,
        true,
        false,
        false,
        false,
        false
    );
}


bool RendererRayTracingSystem::renderSurfelGiSpawn(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const bool graphEntryStatesOwned
){
    return renderSurfelGiPhases(
        commandList,
        targets,
        deferredLightingResources,
        graphEntryStatesOwned,
        false,
        false,
        true,
        false,
        false,
        false,
        false,
        true,
        true,
        false,
        false,
        false
    );
}


bool RendererRayTracingSystem::renderSurfelGiTraceBuildArgs(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const bool graphEntryStatesOwned
){
    return renderSurfelGiPhases(
        commandList,
        targets,
        deferredLightingResources,
        graphEntryStatesOwned,
        false,
        false,
        false,
        true,
        false,
        false,
        false,
        true,
        true,
        true,
        false,
        false
    );
}


bool RendererRayTracingSystem::renderSurfelGiTrace(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const bool graphEntryStatesOwned
){
    return renderSurfelGiPhases(
        commandList,
        targets,
        deferredLightingResources,
        graphEntryStatesOwned,
        false,
        false,
        false,
        false,
        true,
        false,
        false,
        true,
        true,
        true,
        true,
        false
    );
}


bool RendererRayTracingSystem::renderSurfelGiResolve(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const bool graphEntryStatesOwned
){
    return renderSurfelGiPhases(
        commandList,
        targets,
        deferredLightingResources,
        graphEntryStatesOwned,
        false,
        false,
        false,
        false,
        false,
        true,
        false,
        true,
        true,
        true,
        true,
        true
    );
}


bool RendererRayTracingSystem::renderSurfelGiPhases(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const bool graphEntryStatesOwned,
    const bool dispatchAgeFree,
    const bool dispatchHashBuild,
    const bool dispatchSpawn,
    const bool dispatchTraceBuildArgs,
    const bool dispatchTrace,
    const bool dispatchResolve,
    const bool dispatchRemaining,
    const bool graphOwnsCellHeadClear,
    const bool graphOwnsHashBuild,
    const bool graphOwnsTraceBuildArgs,
    const bool graphOwnsTrace,
    const bool graphOwnsResolve
){
    if(!hasSurfelWork())
        return true;

    NWB_ASSERT(targets.bindless.valid());
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(!heap.isInitialized())
        return false;

    // Only the trace pass is backend-specific.
    const bool useHwTrace = m_rayTracingState.m_surfelUseHwTrace;
    Core::ComputePipeline* const tracePipeline = useHwTrace ? m_rayTracingState.m_surfelTraceHwPipeline.get() : m_rayTracingState.m_surfelTracePipeline.get();

    if(
        !m_rayTracingState.m_surfelSpawnPipeline
        || !m_rayTracingState.m_surfelAgeFreePipeline
        || !m_rayTracingState.m_surfelHashBuildPipeline
        || !tracePipeline
        || !m_rayTracingState.m_surfelResolvePipeline
        || !m_rayTracingState.m_surfelUpsamplePipeline
        || !m_rayTracingState.m_surfelTraceBuildArgsPipeline
    )
        return true;

    if(
        !targets.bindless.valid()
        || !deferredLightingResources.valid()
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelConstantsHeapHandle, Core::GpuDescriptorClass::UniformBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelPoolHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelCellHeadHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelCounterHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelTraceIndirectArgsHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelFreeListHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelPoolSnapshotHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelCellHeadSnapshotHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelMaterialContextSlotsHeapHandle, Core::GpuDescriptorClass::UniformBuffer)
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

    SurfelHeapPushConstants surfelPush;
    surfelPush.constantsHeapSlot = m_rayTracingState.m_surfelConstantsHeapHandle.slot();
    surfelPush.poolHeapSlot = m_rayTracingState.m_surfelPoolHeapHandle.slot();
    surfelPush.cellHeadHeapSlot = m_rayTracingState.m_surfelCellHeadHeapHandle.slot();
    surfelPush.counterHeapSlot = m_rayTracingState.m_surfelCounterHeapHandle.slot();
    surfelPush.freeListHeapSlot = m_rayTracingState.m_surfelFreeListHeapHandle.slot();
    surfelPush.snapshotPoolHeapSlot = m_rayTracingState.m_surfelPoolSnapshotHeapHandle.slot();
    surfelPush.snapshotCellHeadHeapSlot = m_rayTracingState.m_surfelCellHeadSnapshotHeapHandle.slot();
    surfelPush.traceIndirectArgsHeapSlot = m_rayTracingState.m_surfelTraceIndirectArgsHeapHandle.slot();
    surfelPush.deferredResourcesHeapSlot = targets.bindless.slotsBufferDescriptor.slot();
    surfelPush.materialContextSlotsHeapSlot = m_rayTracingState.m_surfelMaterialContextSlotsHeapHandle.slot();

    // Order every in-place field update, including prior-frame spawn writes.
    commandList.setEnableUavBarriersForBuffer(m_rayTracingState.m_surfelPoolBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(m_rayTracingState.m_surfelCellHeadBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(m_rayTracingState.m_surfelCounterBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(m_rayTracingState.m_surfelFreeListBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(m_rayTracingState.m_surfelTraceIndirectArgsBuffer.get(), true);

    // Age-free recycles unseen surfels before the graph-owned cell-head reset and hash rebuild.
    if(dispatchAgeFree){
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_SurfelAgeFree, m_graphics.getDevice(), commandList);
        if(!graphEntryStatesOwned){
            commandList.setBufferState(m_rayTracingState.m_surfelConstants.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(m_rayTracingState.m_surfelPoolBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(m_rayTracingState.m_surfelCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(m_rayTracingState.m_surfelFreeListBuffer.get(), Core::ResourceStates::UnorderedAccess);
        }
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

    // Rebuild occupancy before spawning into empty cells.
    if(!graphOwnsCellHeadClear){
        Core::Buffer* cellHead = m_rayTracingState.m_surfelCellHeadBuffer.get();
        commandList.setBufferState(cellHead, Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        commandList.clearBufferUInt(*cellHead, NWB_SURFEL_CELL_INVALID);
    }

    if(dispatchHashBuild){
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_SurfelHashBuild, m_graphics.getDevice(), commandList);
        if(!graphEntryStatesOwned){
            commandList.setBufferState(m_rayTracingState.m_surfelConstants.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(m_rayTracingState.m_surfelPoolBuffer.get(), Core::ResourceStates::UnorderedAccess);
        }
        if(!graphOwnsCellHeadClear)
            commandList.setBufferState(m_rayTracingState.m_surfelCellHeadBuffer.get(), Core::ResourceStates::UnorderedAccess);
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

    // Spawn claims only empty hash cells.
    if(dispatchSpawn){
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_SurfelSpawn, m_graphics.getDevice(), commandList);
        if(!graphEntryStatesOwned){
            commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
            commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
            commandList.setBufferState(m_rayTracingState.m_surfelConstants.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(m_rayTracingState.m_surfelPoolBuffer.get(), Core::ResourceStates::UnorderedAccess);
        }
        if(!graphOwnsHashBuild)
            commandList.setBufferState(m_rayTracingState.m_surfelCellHeadBuffer.get(), Core::ResourceStates::UnorderedAccess);
        if(!graphEntryStatesOwned){
            commandList.setBufferState(m_rayTracingState.m_surfelCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(m_rayTracingState.m_surfelFreeListBuffer.get(), Core::ResourceStates::UnorderedAccess);
        }
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
        if(!graphEntryStatesOwned){
            commandList.setBufferState(m_rayTracingState.m_surfelConstants.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(m_rayTracingState.m_surfelCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(m_rayTracingState.m_surfelTraceIndirectArgsBuffer.get(), Core::ResourceStates::UnorderedAccess);
        }
        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(m_rayTracingState.m_surfelTraceBuildArgsPipeline.get());
        commandList.setComputeState(state);
        heap.bindCompute(commandList, *m_rayTracingState.m_surfelTraceBuildArgsPipeline.get());
        commandList.setPushConstants(&surfelPush, sizeof(surfelPush));
        commandList.dispatch(
            NWB_SURFEL_TRACE_BUILDARGS_DISPATCH_GROUP_COUNT_X,
            NWB_SURFEL_TRACE_BUILDARGS_DISPATCH_GROUP_COUNT_Y,
            NWB_SURFEL_TRACE_BUILDARGS_DISPATCH_GROUP_COUNT_Z
        );
    }

    if(!dispatchTrace && !dispatchResolve && !dispatchRemaining)
        return true;

    // Direct callers stage heap-selected trace inputs locally; prepared graph callers inherit the compiler-lowered trace-argument state after the graph-owned build-arguments task.
    if(dispatchTrace){
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_SurfelTrace, m_graphics.getDevice(), commandList);
        if(!graphEntryStatesOwned && useHwTrace){
            for(u32 slot = 0u; slot < m_rayTracingState.m_shadowMeshCount; ++slot){
                commandList.setBufferState(m_rayTracingState.m_shadowMeshPositionBuffers[slot], Core::ResourceStates::ShaderResource);
                commandList.setBufferState(m_rayTracingState.m_shadowMeshIndexBuffers[slot], Core::ResourceStates::ShaderResource);
                commandList.setBufferState(m_rayTracingState.m_shadowMeshAttributeBuffers[slot], Core::ResourceStates::ShaderResource);
            }
            commandList.setBufferState(m_rayTracingState.m_shadowInstanceMaterialBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(m_rayTracingState.m_shadowMaterialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(m_rayTracingState.m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
        }
        else if(!graphEntryStatesOwned){
            transitionSwShadowTraversalResources(commandList);
            commandList.setBufferState(m_rayTracingState.m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
        }
        if(!graphEntryStatesOwned && useHwTrace)
            commandList.setAccelStructState(m_rayTracingState.m_tlas.get(), Core::ResourceStates::AccelStructRead);
        if(!graphEntryStatesOwned){
            commandList.setBufferState(m_rayTracingState.m_surfelConstants.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(m_rayTracingState.m_surfelPoolBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(m_rayTracingState.m_surfelPoolSnapshotBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(m_rayTracingState.m_surfelCellHeadSnapshotBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(targets.bindless.slotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(deferredLightingResources.sceneShadingBuffer.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(deferredLightingResources.lightBuffer.get(), Core::ResourceStates::ShaderResource);
        }
        if(!graphOwnsTraceBuildArgs)
            commandList.setBufferState(m_rayTracingState.m_surfelTraceIndirectArgsBuffer.get(), Core::ResourceStates::IndirectArgument);
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
        const Core::GpuDescriptorHandle tlasHeapHandle = m_rayTracingState.m_surfelUseHwTrace
            ? m_rayTracingState.m_tlasHeapHandle
            : Core::GpuDescriptorHandle::invalid();
        heap.bindCompute(commandList, *tracePipeline, tlasHeapHandle);
        commandList.setPushConstants(&surfelPush, sizeof(surfelPush));
        commandList.dispatchIndirect(0u);
    }

    if(!dispatchResolve && !dispatchRemaining)
        return true;

    // Resolve at half resolution so deferred lighting never touches the writable pool.
    if(dispatchResolve){
        const u32 halfWidth = DivideUp(targets.width, static_cast<u32>(NWB_SURFEL_RESOLVE_HALF_FACTOR));
        const u32 halfHeight = DivideUp(targets.height, static_cast<u32>(NWB_SURFEL_RESOLVE_HALF_FACTOR));
        {
            Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_SurfelResolve, m_graphics.getDevice(), commandList);
            if(!graphEntryStatesOwned)
                commandList.setBufferState(m_rayTracingState.m_surfelConstants.get(), Core::ResourceStates::ConstantBuffer);
            if(!graphOwnsTrace){
                commandList.setBufferState(m_rayTracingState.m_surfelPoolBuffer.get(), Core::ResourceStates::ShaderResource);
                commandList.setBufferState(m_rayTracingState.m_surfelCellHeadBuffer.get(), Core::ResourceStates::ShaderResource);
            }
            if(!graphEntryStatesOwned){
                commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
                commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
                commandList.setTextureState(targets.surfelIrradianceHalf.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess);
            }
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
            commandList.dispatch(DivideUp(halfWidth, groupSize), DivideUp(halfHeight, groupSize), 1u);
        }
    }

    if(!dispatchRemaining)
        return true;

    // Surface-aware upsample preserves coverage across edges.
    if(!graphOwnsResolve)
        commandList.setTextureState(targets.surfelIrradianceHalf.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
    {
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_SurfelUpsample, m_graphics.getDevice(), commandList);
        if(!graphEntryStatesOwned){
            commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
            commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        }
        if(!graphEntryStatesOwned)
            commandList.setTextureState(targets.surfelIrradiance.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess);
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

    // The prepared graph declares the actual downstream consumer: live Lighting samples the output, while the lagged route copies it. Keep the compatibility return layout for direct callers, but let graph lowering own the precise UAV-to-SRV or UAV-to-CopySource handoff.
    if(!graphOwnsResolve){
        commandList.setTextureState(targets.surfelIrradiance.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.commitBarriers();
    }

    // The graph-owned late copy publishes its token only after Transfer/Compute/Graphics accepts. This pass only consumes completed diagnostics; resource-state transitions and native copy recording live in that graph task.
    {
        const u32 frameIndex = m_rayTracingState.m_surfelFrameIndex;
        Core::Buffer* readback = m_rayTracingState.m_surfelCounterReadback.get();
        const Core::QueueSubmissionToken submissionToken = m_rayTracingState.m_surfelCountReadbackSubmissionToken;
        const bool submissionComplete =
            submissionToken.valid()
            && submissionToken.hasPhysicalQueueIdentity()
            && m_graphics.getDevice().queueGetCompletedInstance(
                Core::GpuPhysicalQueueId{
                    .index = submissionToken.physicalQueueIndex,
                    .deviceGeneration = submissionToken.deviceGeneration,
                }
            ) >= submissionToken.value
        ;
        if(
            submissionToken.valid()
            && (frameIndex - m_rayTracingState.m_surfelCountReadbackFrame) >= s_SurfelCountLogDelay
            && submissionComplete
        ){
            const u32* counts = static_cast<const u32*>(m_graphics.getDevice().mapBuffer(*readback, Core::CpuAccessMode::Read));
            if(counts){
                const u32 bumpTop = counts[NWB_SURFEL_COUNTER_BUMP_TOP];
                const u32 freeTop = counts[NWB_SURFEL_COUNTER_FREE_TOP];
                m_graphics.getDevice().unmapBuffer(*readback);
                NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: surfel live count = {} (bump {} - free {}) of {} pool capacity")
                    , static_cast<u64>(bumpTop - freeTop)
                    , static_cast<u64>(bumpTop)
                    , static_cast<u64>(freeTop)
                    , static_cast<u64>(m_rayTracingState.m_surfelPoolCapacity)
                );
            }
            m_rayTracingState.m_surfelCountReadbackSubmissionToken = {};
        }
    }

    // Subsequent frames use steady-state round-robin updates.
    m_rayTracingState.m_surfelSeeded = true;
    m_rayTracingState.m_surfelFrameIndex = m_rayTracingState.m_surfelFrameIndex + 1u;
    return true;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

