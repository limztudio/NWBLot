// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_softshadow_pipelines{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool EnsureResolveChannel(
    RendererShaderSystem& shaderSystem,
    Core::GraphicsBackend::Device& device,
    const Core::BindingLayoutHandle& bindingLayout,
    const Name& shaderName,
    SoftShadowResolveChannelState& channel){
    struct StageRequest{
        AStringView m_variant;
        SoftShadowResolveStageState& m_state;
    };
    const StageRequest stages[] = {
        { "NWB_SHADOW_RESOLVE_COMPILED_STAGE=1", channel.m_wavelet },
        { "NWB_SHADOW_RESOLVE_COMPILED_STAGE=2", channel.m_upsample },
    };
    static_assert(NWB_SHADOW_RESOLVE_STAGE_WAVELET == 1u && NWB_SHADOW_RESOLVE_STAGE_UPSAMPLE == 2u);
    auto& heap = device.getDescriptorHeap();
    for(const StageRequest& stage : stages){
        if(stage.m_state.m_pipeline)
            continue;
        if(!shaderSystem.loadShader(stage.m_state.m_shader, shaderName, stage.m_variant, Core::ShaderType::Compute, "ECSRender_SoftShadowResolve")){
            channel.m_failed = true;
            return false;
        }
        Core::ComputePipelineDesc pipelineDesc;
        pipelineDesc
            .setComputeShader(stage.m_state.m_shader)
            .addBindingLayout(bindingLayout)
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout())
        ;
        stage.m_state.m_pipeline = device.createComputePipeline(pipelineDesc);
        if(!stage.m_state.m_pipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create specialized shadow resolve pipeline"));
            channel.m_failed = true;
            return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureShadowGeometryDownsamplePipeline(){
    if(m_rayTracingState.m_shadowGeometryDownsamplePipeline)
        return true;
    if(m_rayTracingState.m_shadowGeometryDownsamplePipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    if(!m_rayTracingState.m_shadowGeometryDownsampleBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ShadowGeometryDownsamplePushConstants)));
        m_rayTracingState.m_shadowGeometryDownsampleBindingLayout = device.createBindingLayout(layoutDesc);
        if(!m_rayTracingState.m_shadowGeometryDownsampleBindingLayout){
            m_rayTracingState.m_shadowGeometryDownsamplePipelineFailed = true;
            return false;
        }
    }
    if(!m_shaderSystem.loadShader(
        m_rayTracingState.m_shadowGeometryDownsampleShader,
        AssetsGraphicsShadow::s_GeometryDownsampleShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_ShadowGeometryDownsample"
    )){
        m_rayTracingState.m_shadowGeometryDownsamplePipelineFailed = true;
        return false;
    }
    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(m_rayTracingState.m_shadowGeometryDownsampleShader)
        .addBindingLayout(m_rayTracingState.m_shadowGeometryDownsampleBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    m_rayTracingState.m_shadowGeometryDownsamplePipeline = device.createComputePipeline(pipelineDesc);
    if(!m_rayTracingState.m_shadowGeometryDownsamplePipeline){
        m_rayTracingState.m_shadowGeometryDownsamplePipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureSoftShadowResolvePipeline(){
    auto& resolve = m_rayTracingState.m_softShadowResolve;
    if(resolve.m_scalar.m_wavelet.m_pipeline && resolve.m_scalar.m_upsample.m_pipeline)
        return true;
    if(resolve.m_scalar.m_failed)
        return false;

    auto& device = m_graphics.getDevice();
    if(!device.getDescriptorHeap().isInitialized())
        return false;
    if(!resolve.m_bindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ShadowResolvePushConstants)));
        resolve.m_bindingLayout = device.createBindingLayout(layoutDesc);
        if(!resolve.m_bindingLayout){
            resolve.m_scalar.m_failed = true;
            return false;
        }
    }
    return __hidden_softshadow_pipelines::EnsureResolveChannel(
        m_shaderSystem, device, resolve.m_bindingLayout, AssetsGraphicsShadow::s_SoftResolveShaderName, resolve.m_scalar
    );
}

bool RendererRayTracingSystem::ensureSoftTransparentResolvePipeline(){
    auto& resolve = m_rayTracingState.m_softShadowResolve;
    if(resolve.m_rgb.m_wavelet.m_pipeline && resolve.m_rgb.m_upsample.m_pipeline)
        return true;
    if(resolve.m_rgb.m_failed || !resolve.m_bindingLayout)
        return false;

    auto& device = m_graphics.getDevice();
    if(!device.getDescriptorHeap().isInitialized())
        return false;
    return __hidden_softshadow_pipelines::EnsureResolveChannel(
        m_shaderSystem, device, resolve.m_bindingLayout, AssetsGraphicsShadow::s_SoftResolveRgbShaderName, resolve.m_rgb
    );
}

// Both trace backends publish the same independent channel textures and use the same optional resolve pipelines.
void RendererRayTracingSystem::prepareSoftCombinedResolvePipelines(){
    if(!m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady)
        return;
    if(!ensureSoftCombinedUpsamplePipeline())
        m_rayTracingState.m_softShadowResolve.m_combinedUpsampleFailed = true;
    if(!ensureSoftCombinedWaveletPipeline())
        m_rayTracingState.m_softShadowResolve.m_combinedWaveletFailed = true;
}

void RendererRayTracingSystem::dispatchSoftShadowResolve(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const u32 slotStart,
    const u32 slotCount,
    const SoftShadowResolveDispatch& dispatch,
    const bool dispatchFirstWavelet,
    const bool dispatchTail
){
    NWB_ASSERT(dispatch.waveletPipeline && dispatch.upsamplePipeline);
    NWB_ASSERT(dispatch.visibilityTexture);
    const u32 halfWidth = (targets.width + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 halfHeight = (targets.height + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 halfGroupsX = DivideUp(halfWidth, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE));
    const u32 halfGroupsY = DivideUp(halfHeight, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE));
    const u32 fullGroupsX = DivideUp(targets.width, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE));
    const u32 fullGroupsY = DivideUp(targets.height, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE));
    const DeferredBindlessFrameResources& bindless = targets.bindless;
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();

    const auto runPass = [&](const SoftShadowResolvePassResources& resources, const u32 stepWidth, const ShadowResolveStage::Enum stage, const u32 groupsX, const u32 groupsY, const bool graphOwnsInputColorState = false, const bool graphOwnsOutputState = false){
        NWB_ASSERT(resources.softHalfTexture && resources.inputColorTexture && resources.momentsTexture && resources.outputTexture);
        switch(stage){
            case ShadowResolveStage::Wavelet:
                if(!dispatch.graphOwnsWaveletGeometryEntryState)
                    commandList.setTextureState(targets.shadowSoftGeometry.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
                if(!graphOwnsInputColorState)
                    commandList.setTextureState(resources.inputColorTexture, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
                if(dispatch.temporalMomentsValid && !dispatch.graphOwnsWaveletMomentsEntryState)
                    commandList.setTextureState(resources.momentsTexture, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
                if(!graphOwnsOutputState)
                    commandList.setTextureState(resources.outputTexture, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
                commandList.setEnableUavBarriersForTexture(resources.outputTexture, true);
                break;
            case ShadowResolveStage::Upsample:
                if(!dispatch.graphOwnsUpsampleStaticEntryStates){
                    commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
                    commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
                    commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
                    commandList.setBufferState(deferredLightingResources.sceneShadingBuffer.get(), Core::ResourceStates::ConstantBuffer);
                }
                if(!graphOwnsInputColorState)
                    commandList.setTextureState(resources.inputColorTexture, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
                if(!graphOwnsOutputState)
                    commandList.setTextureState(dispatch.visibilityTexture, ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
                commandList.setEnableUavBarriersForTexture(dispatch.visibilityTexture, true);
                break;
        }
        commandList.commitBarriers();

        ShadowResolvePushConstants push;
        push.width = targets.width;
        push.height = targets.height;
        push.halfWidth = halfWidth;
        push.halfHeight = halfHeight;
        push.stepWidth = stepWidth;
        push.stage = static_cast<u32>(stage);
        push.lightSlotStart = slotStart;
        push.lightSlotCount = slotCount;
        push.momentsValid = dispatch.temporalMomentsValid ? 1u : 0u;
        push.upsampleFold = static_cast<u32>(dispatch.fold);
        push.geometrySlot = bindless.shadowSoftGeometry.slot();
        push.depthSlot = bindless.gbufferDepth.slot();
        push.worldPositionSlot = bindless.gbufferWorldPosition.slot();
        push.normalSlot = bindless.gbufferNormal.slot();
        push.softHalfSlot = resources.softHalf;
        push.inputColorSlot = resources.inputColor;
        push.momentsSlot = resources.moments;
        push.outputStorageSlot = resources.outputStorage;
        push.visibilityStorageSlot = dispatch.visibilityStorage;
        push.sceneShadingSlot = dispatch.sceneShading;

        Core::ComputePipeline& pipeline = stage == ShadowResolveStage::Upsample
            ? *dispatch.upsamplePipeline
            : *dispatch.waveletPipeline
        ;
        Core::ComputeState state;
        state.setPipeline(&pipeline);
        commandList.setComputeState(state);
        heap.bindCompute(commandList, pipeline);
        commandList.setPushConstants(&push, sizeof(push));
        commandList.dispatch(groupsX, groupsY, 1u);
    };

    static_assert((NWB_SHADOW_RESOLVE_PASS_COUNT % 2) == 1, "opaque resolve pass count must be odd");
    static_assert((NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT % 2) == 1, "transparent resolve pass count must be odd");
    NWB_ASSERT(dispatch.waveletPassCount != 0u && (dispatch.waveletPassCount % 2u) == 1u);
    NWB_ASSERT(dispatchFirstWavelet || dispatchTail);

    if(dispatchFirstWavelet){
        runPass(
            dispatch.firstWaveletResources,
            1u,
            ShadowResolveStage::Wavelet,
            halfGroupsX,
            halfGroupsY,
            dispatch.graphOwnsFirstWaveletInputState,
            dispatch.graphOwnsFirstWaveletOutputState
        );
    }
    if(!dispatchTail)
        return;

    bool sourceIsHalfA = dispatch.firstWaveletWritesHalfA;
    [[maybe_unused]] const SoftShadowResolvePassResources* lastWaveletResources = &dispatch.firstWaveletResources;
    for(u32 pass = 1u; pass < dispatch.waveletPassCount; ++pass){
        const SoftShadowResolvePassResources& nextWaveletResources = sourceIsHalfA
            ? dispatch.outputHalfBResources
            : dispatch.outputHalfAResources
        ;
        runPass(
            nextWaveletResources,
            1u << pass,
            ShadowResolveStage::Wavelet,
            halfGroupsX,
            halfGroupsY
        );
        lastWaveletResources = &nextWaveletResources;
        sourceIsHalfA = !sourceIsHalfA;
    }
    NWB_ASSERT(dispatch.upsampleResources.inputColorTexture == lastWaveletResources->outputTexture);
    runPass(
        dispatch.upsampleResources,
        1u,
        ShadowResolveStage::Upsample,
        fullGroupsX,
        fullGroupsY,
        dispatch.graphOwnsUpsampleInputColorEntryState,
        dispatch.graphOwnsUpsampleVisibilityOutputState
    );
}

bool RendererRayTracingSystem::ensureShadowReprojectMergePipeline(){
    if(m_rayTracingState.m_shadowReprojectMergePipeline)
        return true;
    if(m_rayTracingState.m_shadowReprojectMergePipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    if(!m_rayTracingState.m_shadowReprojectMergeBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ShadowReprojectMergePushConstants)));
        m_rayTracingState.m_shadowReprojectMergeBindingLayout = device.createBindingLayout(layoutDesc);
        if(!m_rayTracingState.m_shadowReprojectMergeBindingLayout){
            m_rayTracingState.m_shadowReprojectMergePipelineFailed = true;
            return false;
        }
    }
    if(!m_shaderSystem.loadShader(
        m_rayTracingState.m_shadowReprojectMergeShader,
        AssetsGraphicsShadow::s_SoftReprojectMergeShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SoftShadowReprojectMerge"
    )){
        m_rayTracingState.m_shadowReprojectMergePipelineFailed = true;
        return false;
    }
    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(m_rayTracingState.m_shadowReprojectMergeShader)
        .addBindingLayout(m_rayTracingState.m_shadowReprojectMergeBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    m_rayTracingState.m_shadowReprojectMergePipeline = device.createComputePipeline(pipelineDesc);
    if(!m_rayTracingState.m_shadowReprojectMergePipeline){
        m_rayTracingState.m_shadowReprojectMergePipelineFailed = true;
        return false;
    }
    return true;
}

void RendererRayTracingSystem::swapSoftShadowTemporalHistory(DeferredFrameTargets& targets){
    if(!m_rayTracingState.m_softShadowTemporalReady)
        return;

    Float44 acceptedWorldToClip = {};
    if(m_meshSystem.snapshotAcceptedMeshViewWorldToClip(acceptedWorldToClip)){
        NWB_MEMCPY(&m_rayTracingState.m_prevWorldToClip, sizeof(m_rayTracingState.m_prevWorldToClip), &acceptedWorldToClip, sizeof(m_rayTracingState.m_prevWorldToClip));
        m_rayTracingState.m_prevWorldToClipValid = true;
    }
    if(m_rayTracingState.m_softTransparentTemporalReady && !hardwareTransparentShadowReady())
        m_rayTracingState.m_softwareTransparentSampling.m_history.accept();
    else
        m_rayTracingState.m_softwareTransparentSampling.m_history.discard();
    m_rayTracingState.m_softShadowTemporalSeeded = true;
    Swap(targets.shadowSoftGeometry, targets.shadowSoftGeometryPrev);
    Swap(targets.bindless.shadowSoftGeometry, targets.bindless.shadowSoftGeometryPrev);
    Swap(targets.bindless.shadowSoftGeometryStorage, targets.bindless.shadowSoftGeometryPrevStorage);
    m_rayTracingState.m_softShadowHistoryFrontIsA ^= 1u;
}

void RendererRayTracingSystem::finalizeSoftShadowTemporalHistory(DeferredFrameTargets& targets){
    if(!m_rayTracingState.m_softShadowTemporalHistoryAdvancePending)
        return;

    m_rayTracingState.m_softShadowTemporalHistoryAdvancePending = false;
    swapSoftShadowTemporalHistory(targets);
}

void RendererRayTracingSystem::discardSoftShadowTemporalHistory(){
    m_rayTracingState.m_softShadowTemporalHistoryAdvancePending = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

