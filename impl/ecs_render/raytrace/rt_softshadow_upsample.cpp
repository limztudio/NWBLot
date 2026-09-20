// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureSoftCombinedUpsamplePipeline(){
    auto& resolve = m_rayTracingState.m_softShadowResolve;
    if(resolve.m_combinedUpsample.m_pipeline)
        return true;
    if(resolve.m_combinedUpsampleFailed || !resolve.m_bindingLayout)
        return false;
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    if(!m_shaderSystem.loadShader(
        resolve.m_combinedUpsample.m_shader,
        AssetsGraphicsShadow::s_SoftResolveCombinedShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_CombinedShadowUpsample"
    )){
        resolve.m_combinedUpsampleFailed = true;
        return false;
    }
    Core::ComputePipelineDesc desc;
    desc
        .setComputeShader(resolve.m_combinedUpsample.m_shader)
        .addBindingLayout(resolve.m_bindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    resolve.m_combinedUpsample.m_pipeline = device.createComputePipeline(desc);
    resolve.m_combinedUpsampleFailed = !resolve.m_combinedUpsample.m_pipeline;
    return !resolve.m_combinedUpsampleFailed;
}

bool RendererRayTracingSystem::renderSoftShadowTerminalUpsample(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const bool transparentReady,
    const bool graphEntryStatesOwned){
    const auto& resolve = m_rayTracingState.m_softShadowResolve;
    const Core::ComputePipelineHandle& pipeline = transparentReady
        ? resolve.m_combinedUpsample.m_pipeline : resolve.m_scalar.m_upsample.m_pipeline;
    if(
        !pipeline
        || !m_rayTracingState.m_softShadowReady
        || m_rayTracingState.m_softShadowSlotMask == 0u
        || NWB_SHADOW_RESOLVE_PASS_COUNT != 1u
        || NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT != 1u
        || !deferredLightingResources.valid()
        || !targets.bindless.valid()
        || !targets.depth
        || !targets.worldPosition
        || !targets.normal
        || !targets.shadowSoftHalfB
        || !targets.shadowSoftGeometry
        || !targets.shadowVisibility
        || (transparentReady && (!m_rayTracingState.m_softTransparentReady || !targets.shadowSoftHalfA))
    )
        return false;

    if(!graphEntryStatesOwned){
        commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.normal.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.shadowSoftGeometry.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.shadowSoftHalfB.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
        if(transparentReady)
            commandList.setTextureState(targets.shadowSoftHalfA.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
        commandList.setBufferState(deferredLightingResources.sceneShadingBuffer.get(), Core::ResourceStates::ConstantBuffer);
        commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
    }
    commandList.setEnableUavBarriersForTexture(targets.shadowVisibility.get(), true);
    commandList.commitBarriers();

    const DeferredBindlessFrameResources& bindless = targets.bindless;
    ShadowResolvePushConstants push;
    push.width = targets.width;
    push.height = targets.height;
    push.halfWidth = DivideUp(targets.width, static_cast<u32>(NWB_SW_SHADOW_SOFT_FACTOR));
    push.halfHeight = DivideUp(targets.height, static_cast<u32>(NWB_SW_SHADOW_SOFT_FACTOR));
    push.stage = NWB_SHADOW_RESOLVE_STAGE_UPSAMPLE;
    for(u32 slot = 0u; slot < NWB_SCENE_SHADOW_SLOT_COUNT; ++slot){
        if((m_rayTracingState.m_softShadowSlotMask & (1u << slot)) != 0u)
            push.lightSlotCount = slot + 1u;
    }
    push.geometrySlot = bindless.shadowSoftGeometry.slot();
    push.depthSlot = bindless.gbufferDepth.slot();
    push.worldPositionSlot = bindless.gbufferWorldPosition.slot();
    push.normalSlot = bindless.gbufferNormal.slot();
    push.inputColorSlot = transparentReady ? bindless.shadowSoftHalfA.slot() : bindless.shadowSoftHalfB.slot();
    push.softHalfSlot = push.inputColorSlot;
    push.momentsSlot = push.inputColorSlot;
    push.outputStorageSlot = bindless.shadowVisibilityStorage.slot();
    push.visibilityStorageSlot = bindless.shadowVisibilityStorage.slot();
    push.sceneShadingSlot = bindless.sceneShading.slot();
    push.opaqueInputColorSlot = bindless.shadowSoftHalfB.slot();
    Core::ComputeState state;
    state.setPipeline(pipeline.get());
    commandList.setComputeState(state);
    m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *pipeline.get());
    commandList.setPushConstants(&push, sizeof(push));
    commandList.dispatch(
        DivideUp(targets.width, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE)),
        DivideUp(targets.height, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE)),
        1u
    );
    // The original terminal fold is the only successful history-advance endpoint. Opaque recovery does not advance it.
    if(transparentReady && m_rayTracingState.m_softShadowTemporalReady)
        m_rayTracingState.m_softShadowTemporalHistoryAdvancePending = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

