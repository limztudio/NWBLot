// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "hardware_transparent_shadow_state.h"

#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/assets/graphics/shadow/hardware_transparent_binding_slots.h>

#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_hardware_transparent_shadow{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct PushConstants{
    u32 width;
    u32 height;
    u32 frameIndex;
    u32 lightSlot;
    u32 sampleIndex;
    u32 sampleCount;
    u32 deferredResourcesHeapSlot;
    u32 materialContextSlotsHeapSlot;
    u32 crossingsHeapSlot;
    u32 overflowListHeapSlot;
    u32 overflowArgsHeapSlot;
    u32 outputStorageSlot;
};

static_assert(sizeof(PushConstants) == NWB_HW_TRANSPARENT_PUSH_CONSTANT_BYTES);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::prepareHardwareTransparentShadowResources(DeferredFrameTargets& targets){
    auto& state = m_rayTracingState.m_hardwareTransparentShadow;
    state.m_ready = false;
    if(state.m_pipelineFailed)
        return false;
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout())
        return false;
    if(!state.m_bindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0u, sizeof(__hidden_hardware_transparent_shadow::PushConstants)));
        state.m_bindingLayout = device.createBindingLayout(layoutDesc);
        if(!state.m_bindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create hardware transparent-shadow binding layout"));
            state.m_pipelineFailed = true;
            return false;
        }
    }
    const Name shaderNames[] = {
        AssetsGraphicsShadow::s_HardwareTransparentGatherShaderName,
        AssetsGraphicsShadow::s_HardwareTransparentEvaluateShaderName,
        AssetsGraphicsShadow::s_HardwareTransparentOverflowShaderName,
    };
    for(u32 i = 0u; i < LengthOf(state.m_pipelines); ++i){
        if(state.m_pipelines[i])
            continue;
        if(!m_shaderSystem.loadShader(
            state.m_shaders[i], shaderNames[i], AStringView("NWB_BINDLESS_TLAS=1"), Core::ShaderType::Compute,
            "ECSRender_HardwareTransparentShadow"
        )){
            state.m_pipelineFailed = true;
            return false;
        }
        Core::ComputePipelineDesc pipelineDesc;
        pipelineDesc
            .setComputeShader(state.m_shaders[i])
            .addBindingLayout(state.m_bindingLayout)
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout())
            .addBindingLayout(heap.getAccelStructLayout())
        ;
        state.m_pipelines[i] = device.createComputePipeline(pipelineDesc);
        if(!state.m_pipelines[i]){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create hardware transparent-shadow pipeline {}"), i);
            state.m_pipelineFailed = true;
            return false;
        }
    }

    const u64 halfWidth = DivideUp(static_cast<u64>(targets.width), static_cast<u64>(NWB_SW_SHADOW_SOFT_FACTOR));
    const u64 halfHeight = DivideUp(static_cast<u64>(targets.height), static_cast<u64>(NWB_SW_SHADOW_SOFT_FACTOR));
    const u64 pixels = halfWidth * halfHeight;
    if(pixels == 0u || pixels > Limit<u32>::s_Max / NWB_HW_TRANSPARENT_WORDS_PER_RAY){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: hardware transparent-shadow scratch extent is invalid"));
        return false;
    }
    if(pixels > state.m_pixelsCapacity){
        const u64 sizes[] = {
            pixels * NWB_HW_TRANSPARENT_WORDS_PER_RAY * sizeof(u32),
            pixels * sizeof(u32), NWB_HW_TRANSPARENT_OVERFLOW_ARGS_WORDS * sizeof(u32),
        };
        const Name names[] = {
            Name("hardware_shadow_crossings"), Name("hardware_shadow_overflow_list"), Name("hardware_shadow_overflow_args"),
        };
        Core::BufferHandle buffers[3];
        Core::GpuDescriptorHandle descriptors[3]{};
        ScopeExit retireUnpublished([&]()noexcept{
            for(auto& handle : descriptors)
                RayTracingDetail::RetireHeapHandle(heap, handle);
        });
        for(u32 i = 0u; i < LengthOf(buffers); ++i){
            Core::BufferDesc desc;
            desc
                .setByteSize(sizes[i])
                .setStructStride(sizeof(u32))
                .setCanHaveUAVs(true)
                .setIsDrawIndirectArgs(i == 2u)
                .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
                .setDebugName(names[i])
                .enableAutomaticStateTracking(Core::ResourceStates::UnorderedAccess)
            ;
            buffers[i] = m_graphics.createBuffer(desc);
            if(!buffers[i] || !RayTracingDetail::RegisterHeapBuffer(
                heap, *buffers[i], Core::GpuDescriptorClass::StorageBuffer, true, descriptors[i]
            )){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create hardware transparent-shadow scratch {}"), i);
                return false;
            }
        }
        RayTracingDetail::RetireHeapHandle(heap, state.m_crossingsHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, state.m_overflowListHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, state.m_overflowArgsHeapHandle);
        state.m_crossingsBuffer = Move(buffers[0]);
        state.m_overflowListBuffer = Move(buffers[1]);
        state.m_overflowArgsBuffer = Move(buffers[2]);
        state.m_crossingsHeapHandle = descriptors[0];
        state.m_overflowListHeapHandle = descriptors[1];
        state.m_overflowArgsHeapHandle = descriptors[2];
        for(auto& handle : descriptors)
            handle = Core::GpuDescriptorHandle::invalid();
        state.m_pixelsCapacity = static_cast<u32>(pixels);
    }
    state.m_ready = true;
    return true;
}

void RendererRayTracingSystem::releaseHardwareTransparentShadowResources(){
    auto& state = m_rayTracingState.m_hardwareTransparentShadow;
    auto& heap = m_graphics.getDevice().getDescriptorHeap();
    if(heap.isInitialized()){
        RayTracingDetail::RetireHeapHandle(heap, state.m_crossingsHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, state.m_overflowListHeapHandle);
        RayTracingDetail::RetireHeapHandle(heap, state.m_overflowArgsHeapHandle);
    }
    state = HardwareTransparentShadowState{};
}

bool RendererRayTracingSystem::hardwareTransparentShadowReady()const noexcept{
    return m_shadowVisibilityHardwareSupported && m_rayTracingState.m_hardwareTransparentShadow.m_ready;
}

void RendererRayTracingSystem::dispatchHardwareTransparentShadow(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const u32 frameIndex,
    const bool graphEntryStatesOwned){
    auto& state = m_rayTracingState.m_hardwareTransparentShadow;
    NWB_ASSERT(state.m_ready);
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    if(!graphEntryStatesOwned){
        commandList.setAccelStructState(m_rayTracingState.m_tlas.get(), Core::ResourceStates::AccelStructRead);
        const auto transitionGeometry = [&](const auto& buffers){
            for(Core::Buffer* buffer : buffers)
                commandList.setBufferState(buffer, Core::ResourceStates::ShaderResource);
        };
        transitionGeometry(m_rayTracingState.m_shadowMeshPositionBuffers);
        transitionGeometry(m_rayTracingState.m_shadowMeshIndexBuffers);
        transitionGeometry(m_rayTracingState.m_shadowMeshAttributeBuffers);
        commandList.setBufferState(m_rayTracingState.m_shadowInstanceMaterialBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(m_rayTracingState.m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(m_rayTracingState.m_shadowMaterialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
        commandList.setBufferState(targets.bindless.slotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
        commandList.setBufferState(deferredLightingResources.sceneShadingBuffer.get(), Core::ResourceStates::ConstantBuffer);
        commandList.setBufferState(deferredLightingResources.lightBuffer.get(), Core::ResourceStates::ShaderResource);
        const auto opticalScene = m_hardwareOpticalScene.snapshot();
        if(opticalScene.valid())
            commandList.setBufferState(opticalScene.buffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
    }
    const u32 halfWidth = DivideUp(targets.width, static_cast<u32>(NWB_SW_SHADOW_SOFT_FACTOR));
    const u32 halfHeight = DivideUp(targets.height, static_cast<u32>(NWB_SW_SHADOW_SOFT_FACTOR));
    const u32 groupsX = DivideUp(halfWidth, static_cast<u32>(NWB_HW_TRANSPARENT_GROUP_SIZE));
    const u32 groupsY = DivideUp(halfHeight, static_cast<u32>(NWB_HW_TRANSPARENT_GROUP_SIZE));
    const u32 sampleCount = transparentShadowSampleCount();
    __hidden_hardware_transparent_shadow::PushConstants push{
        targets.width, targets.height, frameIndex, 0u, 0u, sampleCount,
        targets.bindless.slotsBufferDescriptor.slot(), m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.slot(),
        state.m_crossingsHeapHandle.slot(), state.m_overflowListHeapHandle.slot(), state.m_overflowArgsHeapHandle.slot(),
        targets.bindless.transparentSoftHalfStorage.slot(),
    };
    for(u32 slot = 0u; slot < NWB_SCENE_SHADOW_SLOT_COUNT; ++slot){
        if((m_rayTracingState.m_softShadowSlotMask & (1u << slot)) == 0u)
            continue;
        push.lightSlot = slot;
        for(u32 sample = 0u; sample < sampleCount; ++sample){
            push.sampleIndex = sample;
            const u32 emptyArgs[NWB_HW_TRANSPARENT_OVERFLOW_ARGS_WORDS] = { 0u, 1u, 1u, 0u };
            if(!commandList.tryWriteBuffer(*state.m_overflowArgsBuffer, emptyArgs, sizeof(emptyArgs))){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to reset hardware transparent-shadow continuation arguments"));
                return;
            }
            commandList.setBufferState(state.m_crossingsBuffer.get(), Core::ResourceStates::UnorderedAccess, true);
            commandList.setBufferState(state.m_overflowListBuffer.get(), Core::ResourceStates::UnorderedAccess, true);
            commandList.setBufferState(state.m_overflowArgsBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setTextureState(targets.transparentSoftHalf.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess, true);
            commandList.commitBarriers();
            const auto bind = [&](const u32 stage, const bool indirect){
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

                bind(0u, false);
                commandList.dispatch(groupsX, groupsY, 1u);
            }
            commandList.setBufferState(state.m_crossingsBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(state.m_overflowListBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(state.m_overflowArgsBuffer.get(), Core::ResourceStates::IndirectArgument | Core::ResourceStates::ShaderResource);
            commandList.commitBarriers();
            {
                Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentEvaluate, device, commandList);

                bind(1u, false);
                commandList.dispatch(groupsX, groupsY, 1u);
            }
            // Fast evaluation and continuation write disjoint pixels, but the next sample reads their accumulated result.
            commandList.setTextureState(targets.transparentSoftHalf.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess, true);
            commandList.commitBarriers();
            {
                Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentContinuation, device, commandList);

                bind(2u, true);
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
    if(!state.m_dispatchLogged){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: dispatched hardware transparent shadow traversal ({}x{}, {} instances)")
            , static_cast<u64>(targets.width)
            , static_cast<u64>(targets.height)
            , static_cast<u64>(m_rayTracingState.m_tlasInstanceCount)
        );
        state.m_dispatchLogged = true;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

