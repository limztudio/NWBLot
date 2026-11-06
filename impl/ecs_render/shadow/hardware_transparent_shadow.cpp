// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "hardware_transparent_shadow_state.h"

#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/assets/graphics/shadow/hardware_transparent_binding_slots.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(HardwareTransparentShadowPushConstants) == NWB_HW_TRANSPARENT_PUSH_CONSTANT_BYTES);
static_assert(offsetof(HardwareTransparentShadowPushConstants, receiverFactor) == s_HwTransparentReceiverFactorByteOffset);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::prepareHardwareTransparentShadowResources(DeferredFrameTargets& targets){
    auto& state = m_rayTracingState.m_hardwareTransparentShadow;
    state.m_ready = false;
    if(state.m_pipelineFailed)
        return false;
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout())
        return false;
    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(HardwareTransparentShadowPushConstants), Core::ShaderType::Compute, state.m_bindingLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create hardware transparent-shadow binding layout"));
        state.m_pipelineFailed = true;
        return false;
    }
    const Array<const Core::Assets::AssetRef<ComputeShader> ShadowSubsystem::*, 3u> shaderMembers = {
        &ShadowSubsystem::hardwareTransparentGatherCs,
        &ShadowSubsystem::hardwareTransparentEvaluateCs,
        &ShadowSubsystem::hardwareTransparentOverflowCs,
    };
    for(u32 i = 0u; i < LengthOf(state.m_pipelines); ++i){
        if(state.m_pipelines[i])
            continue;
        if(!m_shaderSystem.loadShader<ComputeShader>(shaderMembers[i], Core::ShaderArchive::s_DefaultVariant, "ECSRender_HardwareTransparentShadow", state.m_shaders[i])){
            state.m_pipelineFailed = true;
            return false;
        }
        if(!RendererComputePipelineDetail::CreateHeapComputePipeline(device, state.m_shaders[i], state.m_bindingLayout, true, state.m_pipelines[i])){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create hardware transparent-shadow pipeline {}"), i);
            state.m_pipelineFailed = true;
            return false;
        }
    }

    u32 pixels = 0u;
    Array<u64, 3u> sizes{};
    if(!ValidateHardwareTransparentShadowScratchExtent(
        targets.width,
        targets.height,
        targets.shadowReceiverFactor,
        NWB_HW_TRANSPARENT_WORDS_PER_RAY,
        NWB_HW_TRANSPARENT_OVERFLOW_ARGS_WORDS,
        pixels,
        sizes
    ))
        return false;
    if(pixels > state.m_pixelsCapacity){
        const Array<Name, 3u> names = { Name("hardware_shadow_crossings"), Name("hardware_shadow_overflow_list"), Name("hardware_shadow_overflow_args"), };
        Array<Core::BufferHandle, s_HwTransparentScratchBufferCount> buffers{};
        Array<Core::GpuDescriptorHandle, s_HwTransparentScratchBufferCount> descriptors{};
        Core::GpuDescriptorRetirementGuard retireUnpublished(heap, Span<const Core::GpuDescriptorHandle>(descriptors));
        for(u32 i = 0u; i < buffers.size(); ++i){
            Core::BufferDesc desc;
            desc
                .setByteSize(sizes[i])
                .setStructStride(sizeof(u32))
                .setCanHaveUAVs(true)
                .setIsDrawIndirectArgs(i == s_HwTransparentOverflowArgsBufferIndex)
                .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
                .setDebugName(names[i])
                .enableAutomaticStateTracking(Core::ResourceStates::UnorderedAccess)
            ;
            auto buffersResult = m_graphics.createBuffer(desc);
            if(buffersResult)
                buffers[i] = Move(*buffersResult);
            else
                buffers[i].reset();
            if(!buffersResult){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create hardware transparent-shadow scratch {}"), i);
                return false;
            }
            const auto descriptor = RayTracingDetail::RegisterHeapBuffer(heap, *buffers[i], Core::GpuDescriptorClass::StorageBuffer, true);
            if(!descriptor){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create hardware transparent-shadow scratch {}"), i);
                return false;
            }
            descriptors[i] = *descriptor;
        }
        RayTracingDetail::RetireHeapHandle(heap, state.m_crossingsHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, state.m_overflowListHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, state.m_overflowArgsHeapHandle);
        state.m_crossingsBuffer = Move(buffers[s_HwTransparentCrossingsBufferIndex]);
        state.m_overflowListBuffer = Move(buffers[s_HwTransparentOverflowListBufferIndex]);
        state.m_overflowArgsBuffer = Move(buffers[s_HwTransparentOverflowArgsBufferIndex]);
        state.m_crossingsHeapHandle = descriptors[s_HwTransparentCrossingsBufferIndex];
        state.m_overflowListHeapHandle = descriptors[s_HwTransparentOverflowListBufferIndex];
        state.m_overflowArgsHeapHandle = descriptors[s_HwTransparentOverflowArgsBufferIndex];
        for(auto& handle : descriptors)
            handle = Core::GpuDescriptorHandle::Invalid();
        state.m_pixelsCapacity = static_cast<u32>(pixels);
    }
    state.m_ready = true;
    return true;
}

void RendererRayTracingSystem::releaseHardwareTransparentShadowResources(){
    auto& state = m_rayTracingState.m_hardwareTransparentShadow;
    auto& heap = m_graphics.getDevice().getDescriptorHeap();
    RayTracingDetail::RetireHeapHandle(heap, state.m_crossingsHeapHandle);
    RayTracingDetail::RetireHeapHandle(heap, state.m_overflowListHeapHandle);
    RayTracingDetail::RetireHeapHandle(heap, state.m_overflowArgsHeapHandle);
    state = HardwareTransparentShadowState{};
}

b8 RendererRayTracingSystem::hardwareTransparentShadowReady()const noexcept{
    return m_shadowVisibilityHardwareSupported && m_rayTracingState.m_hardwareTransparentShadow.m_ready;
}

void RendererRayTracingSystem::dispatchHardwareTransparentShadow(Core::CommandList& commandList, DeferredFrameTargets& targets, const u32 frameIndex){
    auto& state = m_rayTracingState.m_hardwareTransparentShadow;
    NWB_ASSERT(state.m_ready);
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    const u32 halfWidth = DivideUp(targets.width, static_cast<u32>(targets.shadowReceiverFactor));
    const u32 halfHeight = DivideUp(targets.height, static_cast<u32>(targets.shadowReceiverFactor));
    const u32 groupsX = DivideUp(halfWidth, static_cast<u32>(NWB_HW_TRANSPARENT_GROUP_SIZE));
    const u32 groupsY = DivideUp(halfHeight, static_cast<u32>(NWB_HW_TRANSPARENT_GROUP_SIZE));
    const u32 sampleCount = transparentShadowSampleCount();
    HardwareTransparentShadowPushConstants push = ResolveHardwareTransparentShadowPushConstants(
        targets.width,
        targets.height,
        frameIndex,
        sampleCount,
        targets.bindless.slotsBufferDescriptor.slot(),
        m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.slot(),
        state.m_crossingsHeapHandle.slot(),
        state.m_overflowListHeapHandle.slot(),
        state.m_overflowArgsHeapHandle.slot(),
        targets.bindless.transparentSoftHalfStorage.slot(),
        targets.shadowReceiverFactor
    );
    for(u32 slot = 0u; slot < NWB_SCENE_SHADOW_SLOT_COUNT; ++slot){
        if((m_rayTracingState.m_softShadowSlotMask & (1u << slot)) == 0u)
            continue;
        push.lightSlot = slot;
        for(u32 sample = 0u; sample < sampleCount; ++sample){
            push.sampleIndex = sample;
            const Array<u32, NWB_HW_TRANSPARENT_OVERFLOW_ARGS_WORDS> emptyArgs = { 0u, 1u, 1u, 0u };
            if(!commandList.tryWriteBuffer(*state.m_overflowArgsBuffer, emptyArgs.data(), sizeof(emptyArgs))){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to reset hardware transparent-shadow continuation arguments"));
                return;
            }
            commandList.setBufferState(state.m_crossingsBuffer.get(), Core::ResourceStates::UnorderedAccess, true);
            commandList.setBufferState(state.m_overflowListBuffer.get(), Core::ResourceStates::UnorderedAccess, true);
            commandList.setBufferState(state.m_overflowArgsBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setTextureState(targets.transparentSoftHalf.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess, true);
            commandList.commitBarriers();
            const auto bind = [&](const u32 stage, const b8 indirect){
                Core::ComputeState compute;
                compute.setPipeline(state.m_pipelines[stage].get());
                if(indirect)
                    compute.setIndirectParams(state.m_overflowArgsBuffer.get());
                commandList.setComputeState(compute);
                heap.bindCompute(commandList, *state.m_pipelines[stage], m_rayTracingState.m_tlasHeapHandle);
                commandList.setPushConstants(&push, sizeof(push));
            };
            {
                Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentGather, device, commandList);

                bind(s_HwTransparentGatherStage, false);
                commandList.dispatch(groupsX, groupsY, 1u);
            }
            commandList.setBufferState(state.m_crossingsBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(state.m_overflowListBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(state.m_overflowArgsBuffer.get(), Core::ResourceStates::IndirectArgument | Core::ResourceStates::ShaderResource);
            commandList.commitBarriers();
            {
                Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentEvaluate, device, commandList);

                bind(s_HwTransparentEvaluateStage, false);
                commandList.dispatch(groupsX, groupsY, 1u);
            }
            // Fast evaluation and continuation write disjoint pixels, but the next sample reads their accumulated result.
            commandList.setTextureState(targets.transparentSoftHalf.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess, true);
            commandList.commitBarriers();
            {
                Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentContinuation, device, commandList);

                bind(s_HwTransparentOverflowStage, true);
                commandList.dispatchIndirect(0u);
            }
        }
    }
    if(m_rayTracingState.m_softShadowSlotMask != 0u)
        reportTransparentShadowSampling(sampleCount);
    // The trace task exports scratch in its declared UAV state, including the indirect-argument allocation.
    commandList.setBufferState(state.m_crossingsBuffer.get(), Core::ResourceStates::UnorderedAccess);
    commandList.setBufferState(state.m_overflowListBuffer.get(), Core::ResourceStates::UnorderedAccess);
    commandList.setBufferState(state.m_overflowArgsBuffer.get(), Core::ResourceStates::UnorderedAccess);
    commandList.commitBarriers();
    LogHardwareTransparentShadowDispatchIfNeeded(
        targets.width,
        targets.height,
        m_rayTracingState.m_tlasInstanceCount,
        state.m_dispatchLogged
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

