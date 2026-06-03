// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_avboit{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Core::BlendState::RenderTarget BuildAdditiveBlendTarget(const Core::ColorMask::Mask colorWriteMask = Core::ColorMask::All){
    Core::BlendState::RenderTarget target;
    target
        .enableBlend()
        .setSrcBlend(Core::BlendFactor::One)
        .setDestBlend(Core::BlendFactor::One)
        .setBlendOp(Core::BlendOp::Add)
        .setSrcBlendAlpha(Core::BlendFactor::One)
        .setDestBlendAlpha(Core::BlendFactor::One)
        .setBlendOpAlpha(Core::BlendOp::Add)
        .setColorWriteMask(colorWriteMask)
    ;
    return target;
}

static void DispatchAvboitCompute(
    Core::CommandList& commandList,
    Core::ComputePipeline* pipeline,
    Core::BindingSet* bindingSet,
    const AvboitFrameTargets& targets,
    const u32 groupCountX
){
    if(!pipeline || !bindingSet)
        return;

    commandList.setResourceStatesForBindingSet(bindingSet);
    commandList.commitBarriers();

    Core::ComputeState computeState;
    computeState.setPipeline(pipeline);
    computeState.addBindingSet(bindingSet);
    commandList.setComputeState(computeState);

    const RendererAvboitPushConstants pushConstants = BuildRendererAvboitPushConstants(targets);
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.dispatch(groupCountX, 1, 1);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::Format::Enum SelectRendererAvboitAccumColorFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::RGBA16_FLOAT,
        Core::Format::RGBA8_UNORM,
    };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget | Core::FormatSupport::Blendable;

    return ECSRenderDetail::SelectSupportedFormat(device, candidates, requiredSupport);
}

Core::Format::Enum SelectRendererAvboitAccumExtinctionFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::R16_FLOAT,
        Core::Format::R32_FLOAT,
        Core::Format::RGBA16_FLOAT,
        Core::Format::R8_UNORM,
        Core::Format::RGBA8_UNORM,
    };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget | Core::FormatSupport::Blendable;

    return ECSRenderDetail::SelectSupportedFormat(device, candidates, requiredSupport);
}

Core::Format::Enum SelectRendererAvboitTransmittanceFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = {
        NWB_AVBOIT_TRANSMITTANCE_CORE_FORMAT,
    };
    constexpr Core::FormatSupport::Mask requiredSupport =
        Core::FormatSupport::Texture
        | Core::FormatSupport::ShaderSample
        | Core::FormatSupport::ShaderUavStore
    ;

    return ECSRenderDetail::SelectSupportedFormat(device, candidates, requiredSupport);
}

Core::Format::Enum SelectRendererAvboitLowRasterFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::R8_UNORM,
        Core::Format::RGBA8_UNORM,
    };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget;

    return ECSRenderDetail::SelectSupportedFormat(device, candidates, requiredSupport);
}

Core::RenderState BuildRendererAvboitVoxelRenderState(){
    Core::RenderState renderState;
    renderState.depthStencilState.disableDepthTest().disableDepthWrite();
    renderState.rasterState.enableDepthClip().setCullBack();
    renderState.blendState.targets[0].setColorWriteMask(Core::ColorMask::None);
    return renderState;
}

Core::RenderState BuildRendererAvboitAccumulateRenderState(){
    Core::RenderState renderState;
    renderState.depthStencilState
        .enableDepthTest()
        .disableDepthWrite()
        .setDepthFunc(Core::ComparisonFunc::LessOrEqual)
    ;
    renderState.rasterState.enableDepthClip().setCullBack();
    renderState.blendState
        .setRenderTarget(NWB_AVBOIT_ACCUM_COLOR_LOCATION, __hidden_avboit::BuildAdditiveBlendTarget())
        .setRenderTarget(NWB_AVBOIT_ACCUM_EXTINCTION_LOCATION, __hidden_avboit::BuildAdditiveBlendTarget(Core::ColorMask::Red))
    ;
    return renderState;
}

RendererAvboitPushConstants BuildRendererAvboitPushConstants(const AvboitFrameTargets& targets){
    RendererAvboitPushConstants pushConstants;
    pushConstants.frame[NWB_AVBOIT_PUSH_FRAME_FULL_WIDTH] = targets.fullWidth;
    pushConstants.frame[NWB_AVBOIT_PUSH_FRAME_FULL_HEIGHT] = targets.fullHeight;
    pushConstants.frame[NWB_AVBOIT_PUSH_FRAME_LOW_WIDTH] = targets.lowWidth;
    pushConstants.frame[NWB_AVBOIT_PUSH_FRAME_LOW_HEIGHT] = targets.lowHeight;
    pushConstants.volume[NWB_AVBOIT_PUSH_VOLUME_VIRTUAL_SLICE_COUNT] = targets.virtualSliceCount;
    pushConstants.volume[NWB_AVBOIT_PUSH_VOLUME_PHYSICAL_SLICE_COUNT] = targets.physicalSliceCount;
    const u32 physicalExtinctionWordCount = DivideUp(targets.physicalSliceCount, ECSRenderAvboitDetail::s_AvboitExtinctionSlicesPerWord);
    pushConstants.volume[NWB_AVBOIT_PUSH_VOLUME_EXTINCTION_WORD_COUNT] = static_cast<u32>(
        static_cast<u64>(targets.lowWidth) * static_cast<u64>(targets.lowHeight) * static_cast<u64>(physicalExtinctionWordCount)
    );
    pushConstants.volume[NWB_AVBOIT_PUSH_VOLUME_COVERAGE_WORD_COUNT] = DivideUp(targets.virtualSliceCount, NWB_AVBOIT_COVERAGE_SLICES_PER_WORD);
    pushConstants.params.raw[NWB_AVBOIT_PUSH_PARAMS_EXTINCTION_FIXED_SCALE] = ECSRenderAvboitDetail::s_AvboitExtinctionFixedScale;
    pushConstants.params.raw[NWB_AVBOIT_PUSH_PARAMS_SELF_OCCLUSION_SLICE_BIAS] = ECSRenderAvboitDetail::s_AvboitSelfOcclusionSliceBias;
    return pushConstants;
}

void RendererAvboitSystem::clearAvboitTargets(Core::CommandList& commandList, AvboitFrameTargets& targets){
    NWB_ASSERT(targets.valid());

    commandList.setTextureState(targets.lowRasterTarget.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.accumColor.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.accumExtinction.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    commandList.setBufferState(targets.coverageBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.setBufferState(targets.depthWarpBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.setBufferState(targets.controlBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.setBufferState(targets.extinctionBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.setBufferState(targets.extinctionOverflowBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.transmittanceTexture.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);

    commandList.commitBarriers();

    const Core::Color transparentBlack(0.f, 0.f, 0.f, 0.f);
    commandList.clearTextureFloat(targets.lowRasterTarget.get(), ECSRenderDetail::s_FramebufferSubresources, transparentBlack);
    commandList.clearTextureFloat(targets.accumColor.get(), ECSRenderDetail::s_FramebufferSubresources, transparentBlack);
    commandList.clearTextureFloat(targets.accumExtinction.get(), ECSRenderDetail::s_FramebufferSubresources, transparentBlack);
    commandList.clearBufferUInt(targets.coverageBuffer.get(), 0u);
    commandList.clearBufferUInt(targets.depthWarpBuffer.get(), 0u);
    commandList.clearBufferUInt(targets.controlBuffer.get(), 0u);
    commandList.clearBufferUInt(targets.extinctionBuffer.get(), 0u);
    commandList.clearBufferUInt(targets.extinctionOverflowBuffer.get(), NWB_AVBOIT_OVERFLOW_INVALID);
    commandList.clearTextureFloat(targets.transmittanceTexture.get(), ECSRenderDetail::s_FramebufferSubresources, Core::Color(1.f, 1.f, 1.f, 1.f));
}

void RendererAvboitSystem::renderAvboitPasses(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgFrameState& csgFrameState
){
    AvboitFrameTargets& avboitTargets = targets.avboit;
    NWB_ASSERT(avboitTargets.valid());
    if((!avboitState().m_depthWarpPipeline || !avboitState().m_integratePipeline) && !createAvboitPipelines())
        return;

    m_renderer.materialSystem().renderMaterialPass(
        commandList,
        avboitTargets.lowFramebuffer.get(),
        MaterialPipelinePass::AvboitOccupancy,
        true,
        csgFrameState,
        avboitTargets.occupancyBindingSet.get(),
        &avboitTargets
    );
    commandList.endRenderPass();

    dispatchAvboitDepthWarp(commandList, avboitTargets);

    m_renderer.materialSystem().renderMaterialPass(
        commandList,
        avboitTargets.lowFramebuffer.get(),
        MaterialPipelinePass::AvboitExtinction,
        true,
        csgFrameState,
        avboitTargets.extinctionBindingSet.get(),
        &avboitTargets
    );
    commandList.endRenderPass();

    dispatchAvboitIntegration(commandList, avboitTargets);

    m_renderer.materialSystem().renderMaterialPass(
        commandList,
        avboitTargets.accumulationFramebuffer.get(),
        MaterialPipelinePass::AvboitAccumulate,
        true,
        csgFrameState,
        avboitTargets.accumulateBindingSet.get(),
        &avboitTargets
    );
    commandList.endRenderPass();
}

void RendererAvboitSystem::dispatchAvboitDepthWarp(Core::CommandList& commandList, AvboitFrameTargets& targets){
    __hidden_avboit::DispatchAvboitCompute(
        commandList,
        avboitState().m_depthWarpPipeline.get(),
        targets.depthWarpBindingSet.get(),
        targets,
        NWB_AVBOIT_DEPTH_WARP_DISPATCH_GROUP_COUNT_X
    );
}

void RendererAvboitSystem::dispatchAvboitIntegration(Core::CommandList& commandList, AvboitFrameTargets& targets){
    const u32 pixelCount = targets.lowWidth * targets.lowHeight;
    __hidden_avboit::DispatchAvboitCompute(
        commandList,
        avboitState().m_integratePipeline.get(),
        targets.integrateBindingSet.get(),
        targets,
        DivideUp(pixelCount, static_cast<u32>(NWB_AVBOIT_INTEGRATE_GROUP_SIZE_X))
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

