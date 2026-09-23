// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <core/task/gpu/compiled_graph.h>
#include <global/algorithm.h>
#include <impl/ecs_render/raytrace/rt_shadow_tasks.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::clearShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets){
    if(!targets.shadowVisibility)
        return;

    // White transmittance is the all-lit fallback.
    commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.clearTextureFloat(*targets.shadowVisibility, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::Color(1.f, 1.f, 1.f, 1.f));
}

bool RendererRayTracingSystem::renderGpuBvhShadowVisibility(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const bool graphEntryStatesOwned,
    const bool splitSoftTransparentFold,
    u32* const opaqueFrameIndex,
    const bool graphOwnsOpaqueTemporalMergeEntryStates,
    const bool splitOpaqueSoftResolve,
    const GraphOwnedAdaptiveShadowPlan* const graphOwnedAdaptivePlan,
    const LightSpaceShadowSnapshot* const lightSpace
){
    NWB_ASSERT(!splitOpaqueSoftResolve || splitSoftTransparentFold);
    if(!targets.shadowVisibility)
        return false;
    NWB_ASSERT(targets.bindless.valid());
    NWB_ASSERT(deferredLightingResources.valid());
    if(!m_rayTracingState.m_sceneBvhNodeBuffer || m_rayTracingState.m_sceneBvhInstanceCount == 0u)
        return false;
    if(!m_rayTracingState.m_swShadowOpaquePrepassPipeline || m_rayTracingState.m_swShadowMeshCount == 0u)
        return false;

    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(
        !heap.isInitialized()
        || !targets.bindless.valid()
        || !RayTracingDetail::IsHeapHandle(targets.bindless.slotsBufferDescriptor, Core::GpuDescriptorClass::UniformBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle, Core::GpuDescriptorClass::UniformBuffer)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowVisibilityStorage, Core::GpuDescriptorClass::StorageImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowCoarseTransmittanceStorage, Core::GpuDescriptorClass::StorageImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowSoftHalfAStorage, Core::GpuDescriptorClass::StorageImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.transparentSoftHalfStorage, Core::GpuDescriptorClass::StorageImage)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_swShadowEdgeStatsHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_swShadowEdgeCounterHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_swShadowEdgeListHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_swShadowIndirectArgsHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software-shadow heap resources are incomplete"));
        return false;
    }

    Optional<Core::GpuTimingMeasure> timing;
    if(!splitSoftTransparentFold)
        timing.emplace(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowVisibility, m_graphics.getDevice(), commandList);

    if(!graphEntryStatesOwned){
        // BVH build leaves traversal inputs in UAV state. Direct compatibility callers restore them locally.
        transitionSwShadowTraversalResources(commandList);
        if(m_rayTracingState.m_shadowInstanceBuffer)
            commandList.setBufferState(m_rayTracingState.m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setBufferState(deferredLightingResources.sceneShadingBuffer.get(), Core::ResourceStates::ConstantBuffer);
        commandList.setBufferState(deferredLightingResources.lightBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(targets.bindless.slotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
    }
    // Subsequent visibility passes read/write this UAV in place.
    commandList.setEnableUavBarriersForTexture(targets.shadowVisibility.get(), true);
    if(!graphEntryStatesOwned)
        commandList.commitBarriers();

    const auto passState = [&](const Core::ComputePipelineHandle& pipeline){
        Core::ComputeState state;
        state.setPipeline(pipeline.get());
        return state;
    };

    const auto bindPassHeap = [&](const Core::ComputePipelineHandle& pipeline){
        heap.bindCompute(commandList, *pipeline.get());
    };

    const auto makePush = [&](){
        SwShadowHeapPushConstants push;
        push.instanceCount = m_rayTracingState.m_sceneBvhInstanceCount;
        push.deferredResourcesHeapSlot = targets.bindless.slotsBufferDescriptor.slot();
        push.materialContextSlotsHeapSlot = m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.slot();
        push.visibilityStorageSlot = targets.bindless.shadowVisibilityStorage.slot();
        push.coarseStorageSlot = targets.bindless.shadowCoarseTransmittanceStorage.slot();
        push.softHalfStorageSlot = targets.bindless.shadowSoftHalfAStorage.slot();
        push.transparentSoftHalfStorageSlot = targets.bindless.transparentSoftHalfStorage.slot();
        push.edgeStatsStorageSlot = m_rayTracingState.m_swShadowEdgeStatsHeapHandle.slot();
        push.edgeCounterStorageSlot = m_rayTracingState.m_swShadowEdgeCounterHeapHandle.slot();
        push.edgeListStorageSlot = m_rayTracingState.m_swShadowEdgeListHeapHandle.slot();
        push.indirectArgsStorageSlot = m_rayTracingState.m_swShadowIndirectArgsHeapHandle.slot();
        return push;
    };

    const u32 groupSize = static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE);
    const u32 fullGroupsX = DivideUp(targets.width, groupSize);
    const u32 fullGroupsY = DivideUp(targets.height, groupSize);
    const u32 coarseWidth = (targets.width + NWB_SW_SHADOW_COARSE_FACTOR - 1u) / NWB_SW_SHADOW_COARSE_FACTOR;
    const u32 coarseHeight = (targets.height + NWB_SW_SHADOW_COARSE_FACTOR - 1u) / NWB_SW_SHADOW_COARSE_FACTOR;
    const u32 coarseGroupsX = DivideUp(coarseWidth, groupSize);
    const u32 coarseGroupsY = DivideUp(coarseHeight, groupSize);

    // Skip the fallback after a soft transparent fold.
    bool softTransparentRan = false;

    // Soft upsample overwrites the opaque prepass.
    const bool softWillRun = m_rayTracingState.m_softShadowReady && m_rayTracingState.m_softShadowSlotMask != 0u;
    if(!softWillRun){
        commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        SwShadowHeapPushConstants opaquePush = makePush();
        opaquePush.width = targets.width;
        opaquePush.height = targets.height;
        commandList.setComputeState(passState(m_rayTracingState.m_swShadowOpaquePrepassPipeline));
        bindPassHeap(m_rayTracingState.m_swShadowOpaquePrepassPipeline);
        commandList.setPushConstants(&opaquePush, sizeof(opaquePush));
        commandList.dispatch(fullGroupsX, fullGroupsY, 1u);
    }

    // The transparent pass reads and multiplies the opaque mask in place.
    commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
    commandList.commitBarriers();

    // Soft opaque resolve replaces the full-resolution mask.
    if(softWillRun){
        const u32 softHalfWidth = (targets.width + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
        const u32 softHalfHeight = (targets.height + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
        const u32 softGroupsX = DivideUp(softHalfWidth, groupSize);
        const u32 softGroupsY = DivideUp(softHalfHeight, groupSize);

        // Advance the primary producer's jitter sequence once.
        const u32 frameIndex = m_rayTracingState.m_softShadowFrameIndex++;

        // Resolve reads the soft trace and geometry scratch in place.
        commandList.setEnableUavBarriersForTexture(targets.shadowSoftHalfA.get(), true);
        commandList.setEnableUavBarriersForTexture(targets.shadowSoftHalfB.get(), true);
        commandList.setEnableUavBarriersForTexture(targets.shadowSoftGeometry.get(), true);
        // Temporal merge writes history before later reads.
        if(m_rayTracingState.m_softShadowTemporalReady){
            commandList.setEnableUavBarriersForTexture(targets.shadowHistA.get(), true);
            commandList.setEnableUavBarriersForTexture(targets.shadowHistB.get(), true);
            commandList.setEnableUavBarriersForTexture(targets.shadowMomentsA.get(), true);
            commandList.setEnableUavBarriersForTexture(targets.shadowMomentsB.get(), true);
        }

        commandList.setTextureState(targets.shadowSoftHalfA.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        {
            Core::GpuTimingMeasure opaqueTraceTiming(
                m_graphics.gpuTiming(),
                RendererGpuTimingScope::s_ShadowOpaqueTrace,
                m_graphics.getDevice(),
                commandList
            );
            SwShadowHeapPushConstants softTracePush = makePush();
            softTracePush.width = targets.width;
            softTracePush.height = targets.height;
            softTracePush.frameIndex = frameIndex;
            softTracePush.softSampleCount = softShadowTemporalHistoryUsable()
                ? NWB_SW_SHADOW_SOFT_TEMPORAL_SPP
                : NWB_SW_SHADOW_SOFT_SPP;
            if(lightSpace && lightSpace->ready){
                NWB_ASSERT(graphEntryStatesOwned && splitSoftTransparentFold);
                if(!RecordLightSpaceResolve(
                    commandList, heap, m_graphics.gpuTiming(), *lightSpace, *targets.shadowSoftHalfA, frameIndex, softTracePush.softSampleCount,
                    targets.bindless.shadowSoftHalfAStorage.slot(), false
                ))
                    return false;
                if(!m_lightSpaceShadow.m_dispatchLogged){
                    m_lightSpaceShadow.m_dispatchLogged = true;
                    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: dispatched light-space shadow maps"));
                }
            }
            else{
                commandList.setComputeState(passState(m_rayTracingState.m_swShadowSoftOpaquePipeline));
                bindPassHeap(m_rayTracingState.m_swShadowSoftOpaquePipeline);
                commandList.setPushConstants(&softTracePush, sizeof(softTracePush));
                commandList.dispatch(softGroupsX, softGroupsY, 1u);
            }
        }

        // The split resolver declares this same-UAV dependency, so its graph prologue owns the trace fence.
        // Direct and unsplit compatibility paths retain the established local fence.
        if(!splitOpaqueSoftResolve){
            commandList.setTextureState(targets.shadowSoftHalfA.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();
        }

        // Shared resolve also guards against a second transparent fold.
        dispatchSoftShadowDenoiseAndTransparentFold(
            commandList,
            targets,
            deferredLightingResources,
            frameIndex,
            softGroupsX,
            softGroupsY,
            graphEntryStatesOwned,
            true,
            !splitOpaqueSoftResolve,
            !splitSoftTransparentFold,
            !splitSoftTransparentFold,
            false,
            false,
            false,
            graphOwnsOpaqueTemporalMergeEntryStates,
            false,
            !splitOpaqueSoftResolve,
            false
        );
        if(splitSoftTransparentFold){
            NWB_ASSERT(opaqueFrameIndex);
            if(opaqueFrameIndex)
                *opaqueFrameIndex = frameIndex;
            // The graph-owned transparent tail records in a later callback, so preserve the normal route's
            // one-shot traversal diagnostic before this opaque producer returns.
            reportSoftwareShadowTraversal(targets);
            return true;
        }
        softTransparentRan = m_rayTracingState.m_softTransparentReady;
    }

    // Fallback transparent fold; it is mutually exclusive with the soft path.
    if(!softTransparentRan && m_rayTracingState.m_swShadowAdaptiveEnabled){
        // Compacted mode traces only classified edge records; stats are sampled asynchronously.
        const bool graphOwnsAdaptivePlan =
            graphOwnedAdaptivePlan && graphOwnedAdaptivePlan->enabled
        ;
        const bool compact = graphOwnsAdaptivePlan && graphOwnedAdaptivePlan->compact;
        const bool snapshot = graphOwnsAdaptivePlan && graphOwnedAdaptivePlan->captureStatsSnapshot;
        if(graphOwnsAdaptivePlan && graphOwnedAdaptivePlan->adaptiveRouteRecorded)
            *graphOwnedAdaptivePlan->adaptiveRouteRecorded = true;

        // Coarse transmittance feeds both adaptive resolve modes.
        commandList.setEnableUavBarriersForTexture(targets.shadowCoarseTransmittance.get(), true);
        commandList.setTextureState(targets.shadowCoarseTransmittance.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        SwShadowHeapPushConstants coarsePush = makePush();
        coarsePush.width = targets.width;
        coarsePush.height = targets.height;
        coarsePush.coarseWidth = coarseWidth;
        coarsePush.coarseHeight = coarseHeight;
        commandList.setComputeState(passState(m_rayTracingState.m_swShadowTransparentCoarsePipeline));
        bindPassHeap(m_rayTracingState.m_swShadowTransparentCoarsePipeline);
        commandList.setPushConstants(&coarsePush, sizeof(coarsePush));
        commandList.dispatch(coarseGroupsX, coarseGroupsY, 1u);

        // Synchronize the coarse write before resolve.
        commandList.setTextureState(targets.shadowCoarseTransmittance.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();

        if(compact){
            // The append list is bounded by this frame's reset counter.
            commandList.setEnableUavBarriersForBuffer(m_rayTracingState.m_swShadowEdgeCounterBuffer.get(), true);
            commandList.setEnableUavBarriersForBuffer(m_rayTracingState.m_swShadowEdgeListBuffer.get(), true);
            commandList.setBufferState(m_rayTracingState.m_swShadowEdgeCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(m_rayTracingState.m_swShadowEdgeListBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(m_rayTracingState.m_swShadowIndirectArgsBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();

            // Classify interpolated interiors and append traceable edges.
            commandList.setBufferState(m_rayTracingState.m_swShadowEdgeStatsBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setTextureState(targets.shadowCoarseTransmittance.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();
            SwShadowHeapPushConstants classifyPush = makePush();
            classifyPush.width = targets.width;
            classifyPush.height = targets.height;
            classifyPush.coarseWidth = coarseWidth;
            classifyPush.coarseHeight = coarseHeight;
            classifyPush.edgeThreshold = m_rayTracingState.m_swShadowEdgeThreshold;
            classifyPush.collectStats = snapshot ? 1u : 0u;
            classifyPush.edgeCapacity = m_rayTracingState.m_swShadowEdgeListCapacity;
            commandList.setComputeState(passState(m_rayTracingState.m_swShadowTransparentClassifyPipeline));
            bindPassHeap(m_rayTracingState.m_swShadowTransparentClassifyPipeline);
            commandList.setPushConstants(&classifyPush, sizeof(classifyPush));
            commandList.dispatch(fullGroupsX, fullGroupsY, 1u);

            // Classify produces the list and in-place visibility writes.
            commandList.setBufferState(m_rayTracingState.m_swShadowEdgeCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(m_rayTracingState.m_swShadowEdgeListBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();

            // Build indirect dispatch arguments from the clamped list count.
            commandList.setBufferState(m_rayTracingState.m_swShadowIndirectArgsBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();
            SwShadowHeapPushConstants argsPush = makePush();
            argsPush.traceGroupSize = static_cast<u32>(NWB_SW_SHADOW_TRACE_GROUP);
            argsPush.edgeCapacity = m_rayTracingState.m_swShadowEdgeListCapacity;
            commandList.setComputeState(passState(m_rayTracingState.m_swShadowTransparentBuildArgsPipeline));
            bindPassHeap(m_rayTracingState.m_swShadowTransparentBuildArgsPipeline);
            commandList.setPushConstants(&argsPush, sizeof(argsPush));
            commandList.dispatch(1u, 1u, 1u);

            // Indirect dispatch consumes the generated arguments and list.
            commandList.setBufferState(m_rayTracingState.m_swShadowEdgeCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(m_rayTracingState.m_swShadowEdgeListBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();

            SwShadowHeapPushConstants tracePush = makePush();
            tracePush.width = targets.width;
            tracePush.height = targets.height;
            tracePush.traceGroupSize = static_cast<u32>(NWB_SW_SHADOW_TRACE_GROUP);
            commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(m_rayTracingState.m_swShadowIndirectArgsBuffer.get(), Core::ResourceStates::IndirectArgument);
            commandList.commitBarriers();
            Core::ComputeState computeStateIndirect = passState(m_rayTracingState.m_swShadowTransparentIndirectPipeline);
            computeStateIndirect.setIndirectParams(m_rayTracingState.m_swShadowIndirectArgsBuffer.get());
            commandList.setComputeState(computeStateIndirect);
            bindPassHeap(m_rayTracingState.m_swShadowTransparentIndirectPipeline);
            commandList.setPushConstants(&tracePush, sizeof(tracePush));
            commandList.dispatchIndirect(0u);
        }
        else{
            // Full-resolution adaptive fallback.
            commandList.setTextureState(targets.shadowCoarseTransmittance.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(m_rayTracingState.m_swShadowEdgeStatsBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();
            SwShadowHeapPushConstants resolvePush = makePush();
            resolvePush.width = targets.width;
            resolvePush.height = targets.height;
            resolvePush.coarseWidth = coarseWidth;
            resolvePush.coarseHeight = coarseHeight;
            resolvePush.edgeThreshold = m_rayTracingState.m_swShadowEdgeThreshold;
            resolvePush.collectStats = snapshot ? 1u : 0u;
            commandList.setComputeState(passState(m_rayTracingState.m_swShadowTransparentResolvePipeline));
            bindPassHeap(m_rayTracingState.m_swShadowTransparentResolvePipeline);
            commandList.setPushConstants(&resolvePush, sizeof(resolvePush));
            commandList.dispatch(fullGroupsX, fullGroupsY, 1u);
        }
    }
    else if(!softTransparentRan){
        // Non-adaptive half-resolution transparent fallback.
        commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        SwShadowHeapPushConstants pushConstants = makePush();
        pushConstants.width = targets.width;
        pushConstants.height = targets.height;
        commandList.setComputeState(passState(m_rayTracingState.m_swShadowTransparentUniformPipeline));
        bindPassHeap(m_rayTracingState.m_swShadowTransparentUniformPipeline);
        commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        commandList.dispatch(coarseGroupsX, coarseGroupsY, 1u);
    }

    reportSoftwareShadowTraversal(targets);
    return true;
}

bool RendererRayTracingSystem::renderGpuBvhShadowVisibilityOpaque(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    u32& outFrameIndex,
    const bool graphEntryStatesOwned,
    const bool graphOwnsOpaqueTemporalMergeEntryStates,
    const LightSpaceShadowSnapshot* const lightSpace
){
    outFrameIndex = 0u;
    if(
        !m_rayTracingState.m_softShadowReady
        || !m_rayTracingState.m_softTransparentReady
        || m_rayTracingState.m_softShadowSlotMask == 0u
    )
        return false;
    return renderGpuBvhShadowVisibility(
        commandList,
        targets,
        deferredLightingResources,
        graphEntryStatesOwned,
        true,
        &outFrameIndex,
        graphOwnsOpaqueTemporalMergeEntryStates,
        true,
        nullptr,
        lightSpace
    );
}

bool RendererRayTracingSystem::softTransparentShadowReady()const noexcept{
    return m_rayTracingState.m_softTransparentReady;
}

void RendererRayTracingSystem::appendShadowTraceBindingLayout(Core::BindingLayoutDesc& layoutDesc)const{
    // Trace layouts are push-only; resources come from the global heap.
    static_assert(sizeof(ShadowRqSoftPushConstants) >= sizeof(ShadowRqPushConstants), "shadow-trace push-constant range must cover both the hard and soft trace push structs");
    layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ShadowRqSoftPushConstants)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

