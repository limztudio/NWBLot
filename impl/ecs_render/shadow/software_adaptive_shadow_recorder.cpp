// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "software_adaptive_shadow_recorder.h"

#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SoftwareAdaptiveShadowRecorder::Record(const GraphOwnedAdaptiveShadowPlan* const plan, const RendererRayTracingState& state, Core::CommandList& commandList, DeferredFrameTargets& targets, Core::GpuDescriptorHeap& heap){
    const auto makePush = [&]()noexcept{
        return MakeSwShadowHeapPushConstants(
            state.m_sceneBvhInstanceCount,
            targets.bindless.slotsBufferDescriptor.slot(),
            state.m_rayTraceMaterialContextSlotsHeapHandle.slot(),
            targets.bindless.shadowVisibilityStorage.slot(),
            targets.bindless.shadowCoarseTransmittanceStorage.slot(),
            targets.bindless.shadowSoftHalfAStorage.slot(),
            targets.bindless.transparentSoftHalfStorage.slot(),
            state.m_swShadowEdgeCounterHeapHandle.slot(),
            state.m_swShadowEdgeListHeapHandle.slot(),
            state.m_swShadowIndirectArgsHeapHandle.slot(),
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

    // Compacted mode traces only classified edge records.
    const b8 graphOwnsAdaptivePlan = plan && plan->enabled;
    const b8 compact = graphOwnsAdaptivePlan && plan->compact;

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
    commandList.setComputeState(RayTracingDetail::MakeComputePassState(state.m_swShadowTransparentCoarsePipeline));
    heap.bindCompute(commandList, *state.m_swShadowTransparentCoarsePipeline.get());
    commandList.setPushConstants(&coarsePush, sizeof(coarsePush));
    commandList.dispatch(coarseGroupsX, coarseGroupsY, 1u);

    // Synchronize the coarse write before resolve.
    commandList.setTextureState(targets.shadowCoarseTransmittance.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
    commandList.commitBarriers();

    if(compact){
        // The append list is bounded by this frame's reset counter.
        commandList.setEnableUavBarriersForBuffer(state.m_swShadowEdgeCounterBuffer.get(), true);
        commandList.setEnableUavBarriersForBuffer(state.m_swShadowEdgeListBuffer.get(), true);
        commandList.setBufferState(state.m_swShadowEdgeCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(state.m_swShadowEdgeListBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(state.m_swShadowIndirectArgsBuffer.get(), Core::ResourceStates::UnorderedAccess);
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
        classifyPush.edgeCapacity = state.m_swShadowEdgeListCapacity;
        commandList.setComputeState(RayTracingDetail::MakeComputePassState(state.m_swShadowTransparentClassifyPipeline));
        heap.bindCompute(commandList, *state.m_swShadowTransparentClassifyPipeline.get());
        commandList.setPushConstants(&classifyPush, sizeof(classifyPush));
        commandList.dispatch(fullGroupsX, fullGroupsY, 1u);

        // Classify produces the list and in-place visibility writes.
        commandList.setBufferState(state.m_swShadowEdgeCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(state.m_swShadowEdgeListBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();

        // Build indirect dispatch arguments from the clamped list count.
        commandList.setBufferState(state.m_swShadowIndirectArgsBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
        SwShadowHeapPushConstants argsPush = makePush();
        argsPush.traceGroupSize = static_cast<u32>(NWB_SW_SHADOW_TRACE_GROUP);
        argsPush.edgeCapacity = state.m_swShadowEdgeListCapacity;
        commandList.setComputeState(RayTracingDetail::MakeComputePassState(state.m_swShadowTransparentBuildArgsPipeline));
        heap.bindCompute(commandList, *state.m_swShadowTransparentBuildArgsPipeline.get());
        commandList.setPushConstants(&argsPush, sizeof(argsPush));
        commandList.dispatch(1u, 1u, 1u);

        // Indirect dispatch consumes the generated arguments and list.
        commandList.setBufferState(state.m_swShadowEdgeCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(state.m_swShadowEdgeListBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();

        SwShadowHeapPushConstants tracePush = makePush();
        tracePush.width = targets.width;
        tracePush.height = targets.height;
        tracePush.traceGroupSize = static_cast<u32>(NWB_SW_SHADOW_TRACE_GROUP);
        commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(state.m_swShadowIndirectArgsBuffer.get(), Core::ResourceStates::IndirectArgument);
        commandList.commitBarriers();
        Core::ComputeState computeStateIndirect = RayTracingDetail::MakeComputePassState(state.m_swShadowTransparentIndirectPipeline);
        computeStateIndirect.setIndirectParams(state.m_swShadowIndirectArgsBuffer.get());
        commandList.setComputeState(computeStateIndirect);
        heap.bindCompute(commandList, *state.m_swShadowTransparentIndirectPipeline.get());
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
        commandList.setComputeState(RayTracingDetail::MakeComputePassState(state.m_swShadowTransparentResolvePipeline));
        heap.bindCompute(commandList, *state.m_swShadowTransparentResolvePipeline.get());
        commandList.setPushConstants(&resolvePush, sizeof(resolvePush));
        commandList.dispatch(fullGroupsX, fullGroupsY, 1u);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

