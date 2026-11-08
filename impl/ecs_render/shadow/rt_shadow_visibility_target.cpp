// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>

#include <impl/ecs_render/shadow/light_space_shadow_diagnostics.adhd.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/shadow/types.adhd.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<u32> RendererRayTracingSystem::renderShadowVisibility(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const b8 splitSoftTransparentFold,
    const b8 graphOwnsOpaqueTemporalMergeEntryStates,
    const b8 splitOpaqueSoftResolve,
    const LightSpaceShadowSnapshot* const lightSpace
){
    u32 producedFrameIndex = 0u;
    NWB_ASSERT(!splitOpaqueSoftResolve || splitSoftTransparentFold);
    NWB_ASSERT(deferredLightingResources.valid());
    if(!targets.shadowVisibility)
        return MakeUnexpected(Failure{});
    if(!m_rayTracingState.m_tlas || !m_rayTracingState.m_shadowPipeline)
        return MakeUnexpected(Failure{});

    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(
        !heap.isInitialized()
        || !m_rayTracingState.m_tlasHeapHandle.valid()
        || !targets.bindless.valid()
        || !RayTracingDetail::IsHeapHandle(targets.bindless.slotsBufferDescriptor, Core::GpuDescriptorClass::UniformBuffer)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowVisibilityStorage, Core::GpuDescriptorClass::StorageImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.shadowSoftHalfAStorage, Core::GpuDescriptorClass::StorageImage)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: shadow trace heap resources are incomplete"));
        return MakeUnexpected(Failure{});
    }

    Optional<Core::GpuTimingMeasure> timing;
    if(!splitSoftTransparentFold)
        timing.emplace(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowVisibility, m_graphics.getDevice(), commandList);

    // Hardware tracing shares the half-resolution soft-shadow resolve when available.
    if(m_rayTracingState.m_softShadowReady && m_rayTracingState.m_shadowSoftPipeline && m_rayTracingState.m_softShadowSlotMask != 0u){
        const u32 softGroupsX = ResolveWrappingRayTracingDispatchGroup(targets.width, targets.shadowReceiverFactor, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE));
        const u32 softGroupsY = ResolveWrappingRayTracingDispatchGroup(targets.height, targets.shadowReceiverFactor, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE));


        // Resolve reuses the trace outputs as UAV/SRV scratch.
        enableSoftShadowResolveBarriers(commandList, targets);

        // Advance the jitter sequence once for the primary shadow producer.
        const u32 frameIndex = m_rayTracingState.m_softShadowFrameIndex++;
        producedFrameIndex = frameIndex;

        {
            Core::GpuTimingMeasure opaqueTraceTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowOpaqueTrace, m_graphics.getDevice(), commandList);

            Core::ComputeState softState;
            softState.setPipeline(m_rayTracingState.m_shadowSoftPipeline.get());
            commandList.setComputeState(softState);
            heap.bindCompute(commandList, *m_rayTracingState.m_shadowSoftPipeline.get(), m_rayTracingState.m_tlasHeapHandle);

            const ShadowRqSoftPushConstants softPush = MakeShadowRqSoftPushConstants(
                targets.width,
                targets.height,
                frameIndex,
                targets.bindless.gbufferWorldPosition.slot(),
                targets.bindless.gbufferNormal.slot(),
                targets.bindless.gbufferDepth.slot(),
                targets.bindless.slotsBufferDescriptor.slot(),
                targets.bindless.shadowSoftHalfAStorage.slot(),
                softShadowTemporalHistoryUsable() ? NWB_SW_SHADOW_SOFT_TEMPORAL_SPP : NWB_SW_SHADOW_SOFT_SPP,
                targets.shadowReceiverFactor
            );
            commandList.setPushConstants(&softPush, sizeof(softPush));
            commandList.dispatch(softGroupsX, softGroupsY, 1u);
        }

        if(lightSpace && lightSpace->ready){
            commandList.setTextureState(targets.shadowSoftHalfA.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess, true);
            commandList.commitBarriers();
            if(!RecordLightSpaceResolve(
                *lightSpace,
                frameIndex, softShadowTemporalHistoryUsable() ? NWB_SW_SHADOW_SOFT_TEMPORAL_SPP : NWB_SW_SHADOW_SOFT_SPP,
                targets.bindless.shadowSoftHalfAStorage.slot(), false,
                commandList, heap, m_graphics.gpuTiming(), *targets.shadowSoftHalfA
            ))
                return MakeUnexpected(Failure{});
            LightSpaceShadowLogReporter{}.reportCsgDispatch(
                true,
                true,
                lightSpace->push.instanceCount,
                m_lightSpaceShadow.m_csgDispatchLogged
            );
        }

        // Split resolve declares the trace fence; unsplit soft resolve phases retain their internal UAV fence.
        dispatchPrimarySoftShadowResolve(commandList, targets, deferredLightingResources, frameIndex, softGroupsX, softGroupsY, splitOpaqueSoftResolve, splitSoftTransparentFold, graphOwnsOpaqueTemporalMergeEntryStates);
        return producedFrameIndex;
    }

    // Full-resolution inline-RayQuery fallback.

    {
        Core::GpuTimingMeasure opaqueTraceTiming(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowOpaqueTrace, m_graphics.getDevice(), commandList);

        Core::ComputeState shadowState;
        shadowState.setPipeline(m_rayTracingState.m_shadowPipeline.get());
        commandList.setComputeState(shadowState);
        heap.bindCompute(commandList, *m_rayTracingState.m_shadowPipeline.get(), m_rayTracingState.m_tlasHeapHandle);
        const u32 frameIndex = m_rayTracingState.m_softShadowFrameIndex++;
        producedFrameIndex = frameIndex;
        const ShadowRqPushConstants shadowPush = MakeShadowRqPushConstants(
            frameIndex,
            targets.bindless.gbufferWorldPosition.slot(),
            targets.bindless.gbufferNormal.slot(),
            targets.bindless.gbufferDepth.slot(),
            targets.bindless.slotsBufferDescriptor.slot(),
            targets.bindless.shadowVisibilityStorage.slot()
        );
        commandList.setPushConstants(&shadowPush, sizeof(shadowPush));
        commandList.dispatch(DivideUp(targets.width, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE)), DivideUp(targets.height, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE)), 1u);
    }
    return producedFrameIndex;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

