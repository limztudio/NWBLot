// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/soft_shadow_wavelet.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SoftShadowCombinedWaveletInputs::valid()const noexcept{
    const Core::Texture* textures[] = {
        opaqueHistory, opaqueMoments, transparentHistory, transparentMoments, geometry, opaqueOutput, transparentOutput
    };
    for(usize index = 0u; index < LengthOf(textures); ++index){
        if(!textures[index])
            return false;
        for(usize previous = 0u; previous < index; ++previous){
            if(textures[index] == textures[previous])
                return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureSoftCombinedWaveletPipeline(){
    auto& resolve = m_rayTracingState.m_softShadowResolve;
    if(resolve.m_combinedWavelet.m_pipeline)
        return true;
    if(resolve.m_combinedWaveletFailed)
        return false;
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    if(!resolve.m_combinedWaveletBindingLayout){
        Core::BindingLayoutDesc layout(m_arena);
        layout.setVisibility(Core::ShaderType::Compute);
        layout.addItem(Core::BindingLayoutItem::PushConstants(0u, sizeof(ShadowCombinedWaveletPushConstants)));
        resolve.m_combinedWaveletBindingLayout = device.createBindingLayout(layout);
        if(!resolve.m_combinedWaveletBindingLayout){
            resolve.m_combinedWaveletFailed = true;
            return false;
        }
    }
    if(!m_shaderSystem.loadShader(
        resolve.m_combinedWavelet.m_shader,
        AssetsGraphicsShadow::s_SoftResolveCombinedWaveletShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_CombinedShadowWavelet"
    )){
        resolve.m_combinedWaveletFailed = true;
        return false;
    }
    Core::ComputePipelineDesc desc;
    desc
        .setComputeShader(resolve.m_combinedWavelet.m_shader)
        .addBindingLayout(resolve.m_combinedWaveletBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    resolve.m_combinedWavelet.m_pipeline = device.createComputePipeline(desc);
    resolve.m_combinedWaveletFailed = !resolve.m_combinedWavelet.m_pipeline;
    return !resolve.m_combinedWaveletFailed;
}

bool RendererRayTracingSystem::renderSoftShadowCombinedWavelet(Core::CommandList& commandList, DeferredFrameTargets& targets){
    const auto& resolve = m_rayTracingState.m_softShadowResolve;
    const bool frontIsA = m_rayTracingState.m_softShadowHistoryFrontIsA != 0u;
    if(
        !CanCombineSoftShadowWavelets(
            static_cast<bool>(resolve.m_combinedUpsample.m_pipeline),
            static_cast<bool>(resolve.m_combinedWavelet.m_pipeline),
            m_rayTracingState.m_softShadowTemporalReady,
            m_rayTracingState.m_softTransparentTemporalReady,
            NWB_SHADOW_RESOLVE_PASS_COUNT,
            NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT
        )
        || !m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady
        || !targets.bindless.valid() || m_rayTracingState.m_softShadowSlotMask == 0u
    )
        return false;

    // This graph-only phase inherits both history/moments reads and both distinct output UAV states.
    commandList.setEnableUavBarriersForTexture(targets.shadowSoftHalfB.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.shadowSoftHalfA.get(), true);
    commandList.commitBarriers();
    const DeferredBindlessFrameResources& bindless = targets.bindless;
    ShadowCombinedWaveletPushConstants push;
    push.resolve.width = targets.width;
    push.resolve.height = targets.height;
    push.resolve.halfWidth = DivideUp(targets.width, static_cast<u32>(NWB_SW_SHADOW_SOFT_FACTOR));
    push.resolve.halfHeight = DivideUp(targets.height, static_cast<u32>(NWB_SW_SHADOW_SOFT_FACTOR));
    push.resolve.stage = NWB_SHADOW_RESOLVE_STAGE_WAVELET;
    for(u32 slot = 0u; slot < NWB_SCENE_SHADOW_SLOT_COUNT; ++slot){
        if((m_rayTracingState.m_softShadowSlotMask & (1u << slot)) != 0u)
            push.resolve.lightSlotCount = slot + 1u;
    }
    push.resolve.momentsValid = 1u;
    push.resolve.geometrySlot = bindless.shadowSoftGeometry.slot();
    push.resolve.inputColorSlot = frontIsA ? bindless.transparentHistB.slot() : bindless.transparentHistA.slot();
    push.resolve.softHalfSlot = push.resolve.inputColorSlot;
    push.resolve.momentsSlot = frontIsA ? bindless.transparentMomentsB.slot() : bindless.transparentMomentsA.slot();
    push.resolve.outputStorageSlot = bindless.shadowSoftHalfAStorage.slot();
    push.resolve.opaqueInputColorSlot = frontIsA ? bindless.shadowHistB.slot() : bindless.shadowHistA.slot();
    push.opaqueMomentsSlot = frontIsA ? bindless.shadowMomentsB.slot() : bindless.shadowMomentsA.slot();
    push.opaqueMomentsValid = 1u;
    push.opaqueOutputStorageSlot = bindless.shadowSoftHalfBStorage.slot();
    Core::ComputeState state;
    state.setPipeline(resolve.m_combinedWavelet.m_pipeline.get());
    commandList.setComputeState(state);
    m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *resolve.m_combinedWavelet.m_pipeline.get());
    commandList.setPushConstants(&push, sizeof(push));
    commandList.dispatch(
        DivideUp(push.resolve.halfWidth, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE)),
        DivideUp(push.resolve.halfHeight, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE)),
        1u
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

