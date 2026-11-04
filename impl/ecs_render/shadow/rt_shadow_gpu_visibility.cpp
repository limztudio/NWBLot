// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <global/algorithm.h>


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

RayTracingShadowVisibilityGraphPlanSnapshot RendererRayTracingSystem::snapshotShadowVisibilityGraphPlan(const b8 hardwareShadowSupported)const noexcept{
    const auto& state = m_rayTracingState;
    const b8 hardwareTransparentTrace = hardwareShadowSupported && state.m_softTransparentReady && hardwareTransparentShadowReady();
    const b8 transparentSceneReady = hardwareTransparentTrace ? state.m_tlas && state.m_tlasHeapHandle.valid() : state.m_sceneBvhNodeBuffer && state.m_sceneInstanceBuffer;
    const b8 softTransparentFoldReady =
        state.m_softShadowReady
        && state.m_softShadowSlotMask != 0u
        && softTransparentShadowReady()
        && transparentSceneReady
        && state.m_shadowInstanceMaterialBuffer
        && state.m_shadowMaterialTypedBuffer
        && state.m_shadowInstanceBuffer
    ;
    GraphOwnedAdaptiveShadowPlan adaptivePlan;
    if(!softTransparentShadowReady() && !hardwareShadowSupported && shadowVisibilitySoftwareResourcesPreflighted() && state.m_swShadowEdgeCounterBuffer){
        adaptivePlan.enabled = true;
        adaptivePlan.compact = true;
    }

    return RayTracingShadowVisibilityGraphPlanSnapshot{
        .lightSpace = lightSpaceShadowSnapshot(),
        .adaptivePlan = adaptivePlan,
        .hardwareTransparentTrace = hardwareTransparentTrace,
        .softTransparentFoldReady = softTransparentFoldReady,
        .combinedSoftUpsample = softTransparentFoldReady
            && state.m_softShadowResolve.m_combinedUpsample.m_pipeline
            && NWB_SHADOW_RESOLVE_PASS_COUNT == 1u && NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT == 1u,
        .combinedSoftWaveletReady = state.m_softShadowResolve.m_combinedWavelet.m_pipeline
            && state.m_softShadowTemporalReady && state.m_softTransparentTemporalReady,
        .combinedSoftTemporalReady = static_cast<b8>(state.m_softShadowResolve.m_combinedTemporal.m_pipeline),
        .softShadowHistoryReadable = SoftShadowTemporalHistoryUsable(
            state.m_softShadowTemporalReady,
            state.m_prevWorldToClipValid,
            state.m_softShadowTemporalSeeded
        ),
        .opaqueTemporalMergeReady = state.m_softShadowTemporalReady,
        .transparentTemporalMergeReady = state.m_softTransparentTemporalReady,
        .historyFrontIsA = state.m_softShadowHistoryFrontIsA != 0u,
    };
}

Expected<u32> RendererRayTracingSystem::renderGpuBvhShadowVisibility(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const b8 splitSoftTransparentFold,
    const b8 graphOwnsOpaqueTemporalMergeEntryStates,
    const b8 splitOpaqueSoftResolve,
    const GraphOwnedAdaptiveShadowPlan* const graphOwnedAdaptivePlan,
    const LightSpaceShadowSnapshot* const lightSpace
){
    u32 producedFrameIndex = 0u;
    NWB_ASSERT(!splitOpaqueSoftResolve || splitSoftTransparentFold);
    if(!targets.shadowVisibility)
        return MakeUnexpected(Failure{});
    NWB_ASSERT(targets.bindless.valid());
    NWB_ASSERT(deferredLightingResources.valid());
    if(!m_rayTracingState.m_sceneBvhNodeBuffer || m_rayTracingState.m_sceneBvhInstanceCount == 0u)
        return MakeUnexpected(Failure{});
    if(!m_rayTracingState.m_swShadowOpaquePrepassPipeline || m_rayTracingState.m_swShadowMeshCount == 0u)
        return MakeUnexpected(Failure{});

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
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_swShadowEdgeCounterHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_swShadowEdgeListHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_swShadowIndirectArgsHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: software-shadow heap resources are incomplete"));
        return MakeUnexpected(Failure{});
    }

    Optional<Core::GpuTimingMeasure> timing;
    if(!splitSoftTransparentFold)
        timing.emplace(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowVisibility, m_graphics.getDevice(), commandList);

    // Subsequent visibility passes read/write this UAV in place.
    commandList.setEnableUavBarriersForTexture(targets.shadowVisibility.get(), true);

    const auto makePush = [&]()noexcept{
        return MakeSwShadowHeapPushConstants(
            m_rayTracingState.m_sceneBvhInstanceCount,
            targets.bindless.slotsBufferDescriptor.slot(),
            m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.slot(),
            targets.bindless.shadowVisibilityStorage.slot(),
            targets.bindless.shadowCoarseTransmittanceStorage.slot(),
            targets.bindless.shadowSoftHalfAStorage.slot(),
            targets.bindless.transparentSoftHalfStorage.slot(),
            m_rayTracingState.m_swShadowEdgeCounterHeapHandle.slot(),
            m_rayTracingState.m_swShadowEdgeListHeapHandle.slot(),
            m_rayTracingState.m_swShadowIndirectArgsHeapHandle.slot(),
            targets.shadowReceiverFactor
        );
    };

    const u32 groupSize = static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE);
    const u32 fullGroupsX = DivideUp(targets.width, groupSize);
    const u32 fullGroupsY = DivideUp(targets.height, groupSize);
    const u32 coarseWidth = DivideUpWrapping(targets.width, NWB_SW_SHADOW_COARSE_FACTOR);
    const u32 coarseHeight = DivideUpWrapping(targets.height, NWB_SW_SHADOW_COARSE_FACTOR);
    const u32 coarseGroupsX = DivideUp(coarseWidth, groupSize);
    const u32 coarseGroupsY = DivideUp(coarseHeight, groupSize);

    // Skip the fallback after a soft transparent fold.
    b8 softTransparentRan = false;

    // Soft upsample overwrites the opaque prepass.
    const b8 softWillRun = m_rayTracingState.m_softShadowReady && m_rayTracingState.m_softShadowSlotMask != 0u;
    if(!softWillRun){
        commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        SwShadowHeapPushConstants opaquePush = makePush();
        opaquePush.width = targets.width;
        opaquePush.height = targets.height;
        commandList.setComputeState(RayTracingDetail::MakeComputePassState(m_rayTracingState.m_swShadowOpaquePrepassPipeline));
        heap.bindCompute(commandList, *m_rayTracingState.m_swShadowOpaquePrepassPipeline.get());
        commandList.setPushConstants(&opaquePush, sizeof(opaquePush));
        commandList.dispatch(fullGroupsX, fullGroupsY, 1u);
    }

    // The transparent pass reads and multiplies the opaque mask in place.
    commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
    commandList.commitBarriers();

    // Soft opaque resolve replaces the full-resolution mask.
    if(softWillRun){
        const u32 softHalfWidth = DivideUpWrapping(targets.width, targets.shadowReceiverFactor);
        const u32 softHalfHeight = DivideUpWrapping(targets.height, targets.shadowReceiverFactor);
        const u32 softGroupsX = DivideUp(softHalfWidth, groupSize);
        const u32 softGroupsY = DivideUp(softHalfHeight, groupSize);

        // Advance the primary producer's jitter sequence once.
        const u32 frameIndex = m_rayTracingState.m_softShadowFrameIndex++;
        producedFrameIndex = frameIndex;

        // Resolve reads the soft trace and geometry scratch in place.
        enableSoftShadowResolveBarriers(commandList, targets);

        commandList.setTextureState(targets.shadowSoftHalfA.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        {
            Core::GpuTimingMeasure opaqueTraceTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowOpaqueTrace, m_graphics.getDevice(), commandList);

            SwShadowHeapPushConstants softTracePush = makePush();
            softTracePush.width = targets.width;
            softTracePush.height = targets.height;
            softTracePush.frameIndex = frameIndex;
            softTracePush.softSampleCount = softShadowTemporalHistoryUsable() ? NWB_SW_SHADOW_SOFT_TEMPORAL_SPP : NWB_SW_SHADOW_SOFT_SPP;
            if(lightSpace && lightSpace->ready){
                NWB_ASSERT(splitSoftTransparentFold);
                if(!RecordLightSpaceResolve(*lightSpace, frameIndex, softTracePush.softSampleCount, targets.bindless.shadowSoftHalfAStorage.slot(), false, commandList, heap, m_graphics.gpuTiming(), *targets.shadowSoftHalfA))
                    return MakeUnexpected(Failure{});
                if((lightSpace->push.csgFlags & NWB_CSG_SHADOW_FLAG_ENABLED) != 0u && !m_lightSpaceShadow.m_csgDispatchLogged){
                    m_lightSpaceShadow.m_csgDispatchLogged = true;
                    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: dispatched CSG light-space shadows (hardware_compose=0, {} instances)"), static_cast<u64>(lightSpace->push.instanceCount));
                }
                if(!m_lightSpaceShadow.m_dispatchLogged){
                    m_lightSpaceShadow.m_dispatchLogged = true;
                    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: dispatched light-space shadow maps"));
                }
            }
            else{
                commandList.setComputeState(RayTracingDetail::MakeComputePassState(m_rayTracingState.m_swShadowSoftOpaquePipeline));
                heap.bindCompute(commandList, *m_rayTracingState.m_swShadowSoftOpaquePipeline.get());
                commandList.setPushConstants(&softTracePush, sizeof(softTracePush));
                commandList.dispatch(softGroupsX, softGroupsY, 1u);
            }
        }

        // The split resolver declares this same-UAV dependency, so its graph prologue owns the trace fence.
        // Unsplit soft resolve phases retain their internal UAV fence.
        dispatchPrimarySoftShadowResolve(commandList, targets, deferredLightingResources, frameIndex, softGroupsX, softGroupsY, splitOpaqueSoftResolve, splitSoftTransparentFold, graphOwnsOpaqueTemporalMergeEntryStates);
        if(splitSoftTransparentFold){
            // The transparent tail records later, so preserve the normal route's one-shot traversal diagnostic first.
            reportSoftwareShadowTraversal(targets);
            return producedFrameIndex;
        }
        softTransparentRan = m_rayTracingState.m_softTransparentReady;
    }

    // Fallback transparent fold; it is mutually exclusive with the soft path.
    if(!softTransparentRan){
        // Compacted mode traces only classified edge records.
        const b8 graphOwnsAdaptivePlan = graphOwnedAdaptivePlan && graphOwnedAdaptivePlan->enabled;
        const b8 compact = graphOwnsAdaptivePlan && graphOwnedAdaptivePlan->compact;

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
        commandList.setComputeState(RayTracingDetail::MakeComputePassState(m_rayTracingState.m_swShadowTransparentCoarsePipeline));
        heap.bindCompute(commandList, *m_rayTracingState.m_swShadowTransparentCoarsePipeline.get());
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
            commandList.setTextureState(targets.shadowCoarseTransmittance.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();
            SwShadowHeapPushConstants classifyPush = makePush();
            classifyPush.width = targets.width;
            classifyPush.height = targets.height;
            classifyPush.coarseWidth = coarseWidth;
            classifyPush.coarseHeight = coarseHeight;
            classifyPush.edgeCapacity = m_rayTracingState.m_swShadowEdgeListCapacity;
            commandList.setComputeState(RayTracingDetail::MakeComputePassState(m_rayTracingState.m_swShadowTransparentClassifyPipeline));
            heap.bindCompute(commandList, *m_rayTracingState.m_swShadowTransparentClassifyPipeline.get());
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
            commandList.setComputeState(RayTracingDetail::MakeComputePassState(m_rayTracingState.m_swShadowTransparentBuildArgsPipeline));
            heap.bindCompute(commandList, *m_rayTracingState.m_swShadowTransparentBuildArgsPipeline.get());
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
            Core::ComputeState computeStateIndirect = RayTracingDetail::MakeComputePassState(m_rayTracingState.m_swShadowTransparentIndirectPipeline);
            computeStateIndirect.setIndirectParams(m_rayTracingState.m_swShadowIndirectArgsBuffer.get());
            commandList.setComputeState(computeStateIndirect);
            heap.bindCompute(commandList, *m_rayTracingState.m_swShadowTransparentIndirectPipeline.get());
            commandList.setPushConstants(&tracePush, sizeof(tracePush));
            commandList.dispatchIndirect(0u);
        }
        else{
            // Full-resolution adaptive fallback.
            commandList.setTextureState(targets.shadowCoarseTransmittance.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();
            SwShadowHeapPushConstants resolvePush = makePush();
            resolvePush.width = targets.width;
            resolvePush.height = targets.height;
            resolvePush.coarseWidth = coarseWidth;
            resolvePush.coarseHeight = coarseHeight;
            commandList.setComputeState(RayTracingDetail::MakeComputePassState(m_rayTracingState.m_swShadowTransparentResolvePipeline));
            heap.bindCompute(commandList, *m_rayTracingState.m_swShadowTransparentResolvePipeline.get());
            commandList.setPushConstants(&resolvePush, sizeof(resolvePush));
            commandList.dispatch(fullGroupsX, fullGroupsY, 1u);
        }
    }

    reportSoftwareShadowTraversal(targets);
    return producedFrameIndex;
}

Expected<u32> RendererRayTracingSystem::renderGpuBvhShadowVisibilityOpaque(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const b8 graphOwnsOpaqueTemporalMergeEntryStates,
    const LightSpaceShadowSnapshot* const lightSpace
){
    if(!m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady || m_rayTracingState.m_softShadowSlotMask == 0u)
        return MakeUnexpected(Failure{});
    return renderGpuBvhShadowVisibility(commandList, targets, deferredLightingResources, true, graphOwnsOpaqueTemporalMergeEntryStates, true, nullptr, lightSpace);
}

void RendererRayTracingSystem::enableSoftShadowResolveBarriers(Core::CommandList& commandList, DeferredFrameTargets& targets){
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
}

void RendererRayTracingSystem::dispatchPrimarySoftShadowResolve(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const u32 frameIndex,
    const u32 softGroupsX,
    const u32 softGroupsY,
    const b8 splitOpaqueSoftResolve,
    const b8 splitSoftTransparentFold,
    const b8 graphOwnsOpaqueTemporalMergeEntryStates
){
    // Split resolve declares the trace fence; the direct route retains its UAV fence.
    if(!splitOpaqueSoftResolve){
        commandList.setTextureState(targets.shadowSoftHalfA.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
    }
    dispatchSoftShadowDenoiseAndTransparentFold(
        commandList,
        targets,
        deferredLightingResources,
        frameIndex,
        softGroupsX,
        softGroupsY,
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
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

