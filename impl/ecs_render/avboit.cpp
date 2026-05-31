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

static void SetTextureCopyDestIfValid(Core::CommandList& commandList, const Core::TextureHandle& texture){
    if(texture)
        commandList.setTextureState(texture.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
}

static void SetBufferCopyDestIfValid(Core::CommandList& commandList, const Core::BufferHandle& buffer){
    if(buffer)
        commandList.setBufferState(buffer.get(), Core::ResourceStates::CopyDest);
}

static void ClearTextureFloatIfValid(
    Core::CommandList& commandList,
    const Core::TextureHandle& texture,
    const Core::Color& value
){
    if(texture)
        commandList.clearTextureFloat(texture.get(), ECSRenderDetail::s_FramebufferSubresources, value);
}

static void ClearBufferUIntIfValid(Core::CommandList& commandList, const Core::BufferHandle& buffer, const u32 value){
    if(buffer)
        commandList.clearBufferUInt(buffer.get(), value);
}

static void DispatchAvboitCompute(
    Core::CommandList& commandList,
    Core::ComputePipeline* pipeline,
    Core::BindingSet* bindingSet,
    const RendererSystem::AvboitFrameTargets& targets,
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
        Core::Format::R16_FLOAT,
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
        .setRenderTarget(0, __hidden_avboit::BuildAdditiveBlendTarget())
        .setRenderTarget(1, __hidden_avboit::BuildAdditiveBlendTarget(Core::ColorMask::Red))
    ;
    return renderState;
}

bool MaterialPipelinePassUsesRendererAvboit(const MaterialPipelinePass::Enum pass){
    switch(pass){
    case MaterialPipelinePass::AvboitOccupancy:
    case MaterialPipelinePass::AvboitExtinction:
    case MaterialPipelinePass::AvboitAccumulate:
        return true;
    default:
        return false;
    }
}

RendererAvboitPushConstants BuildRendererAvboitPushConstants(const RendererSystem::AvboitFrameTargets& targets){
    RendererAvboitPushConstants pushConstants;
    pushConstants.frame[0] = targets.fullWidth;
    pushConstants.frame[1] = targets.fullHeight;
    pushConstants.frame[2] = targets.lowWidth;
    pushConstants.frame[3] = targets.lowHeight;
    pushConstants.volume[0] = targets.virtualSliceCount;
    pushConstants.volume[1] = targets.physicalSliceCount;
    const u32 physicalExtinctionWordCount = DivideUp(targets.physicalSliceCount, ECSRenderAvboitDetail::s_AvboitExtinctionSlicesPerWord);
    pushConstants.volume[2] = static_cast<u32>(
        static_cast<u64>(targets.lowWidth) * static_cast<u64>(targets.lowHeight) * static_cast<u64>(physicalExtinctionWordCount)
    );
    pushConstants.volume[3] = DivideUp(targets.virtualSliceCount, NWB_AVBOIT_COVERAGE_SLICES_PER_WORD);
    pushConstants.params = Float4(
        0.f,
        ECSRenderAvboitDetail::s_AvboitExtinctionFixedScale,
        ECSRenderAvboitDetail::s_AvboitSelfOcclusionSliceBias,
        0.f
    );
    return pushConstants;
}

void RendererSystem::clearAvboitTargets(Core::CommandList& commandList, AvboitFrameTargets& targets){
    __hidden_avboit::SetTextureCopyDestIfValid(commandList, targets.lowRasterTarget);
    __hidden_avboit::SetTextureCopyDestIfValid(commandList, targets.accumColor);
    __hidden_avboit::SetTextureCopyDestIfValid(commandList, targets.accumExtinction);
    __hidden_avboit::SetBufferCopyDestIfValid(commandList, targets.coverageBuffer);
    __hidden_avboit::SetBufferCopyDestIfValid(commandList, targets.depthWarpBuffer);
    __hidden_avboit::SetBufferCopyDestIfValid(commandList, targets.controlBuffer);
    __hidden_avboit::SetBufferCopyDestIfValid(commandList, targets.extinctionBuffer);
    __hidden_avboit::SetBufferCopyDestIfValid(commandList, targets.extinctionOverflowBuffer);
    __hidden_avboit::SetTextureCopyDestIfValid(commandList, targets.transmittanceTexture);

    commandList.commitBarriers();

    const Core::Color transparentBlack(0.f, 0.f, 0.f, 0.f);
    __hidden_avboit::ClearTextureFloatIfValid(commandList, targets.lowRasterTarget, transparentBlack);
    __hidden_avboit::ClearTextureFloatIfValid(commandList, targets.accumColor, transparentBlack);
    __hidden_avboit::ClearTextureFloatIfValid(commandList, targets.accumExtinction, transparentBlack);
    __hidden_avboit::ClearBufferUIntIfValid(commandList, targets.coverageBuffer, 0u);
    __hidden_avboit::ClearBufferUIntIfValid(commandList, targets.depthWarpBuffer, 0u);
    __hidden_avboit::ClearBufferUIntIfValid(commandList, targets.controlBuffer, 0u);
    __hidden_avboit::ClearBufferUIntIfValid(commandList, targets.extinctionBuffer, 0u);
    __hidden_avboit::ClearBufferUIntIfValid(commandList, targets.extinctionOverflowBuffer, Limit<u32>::s_Max);
    __hidden_avboit::ClearTextureFloatIfValid(commandList, targets.transmittanceTexture, Core::Color(1.f, 1.f, 1.f, 1.f));
}

void RendererSystem::renderAvboitPasses(Core::CommandList& commandList, DeferredFrameTargets& targets){
    AvboitFrameTargets& avboitTargets = targets.avboit;
    if(!avboitTargets.valid())
        return;
    if((!m_avboitDepthWarpPipeline || !m_avboitIntegratePipeline) && !createAvboitPipelines())
        return;

    renderMaterialPass(
        commandList,
        avboitTargets.lowFramebuffer.get(),
        MaterialPipelinePass::AvboitOccupancy,
        true,
        avboitTargets.occupancyBindingSet.get(),
        &avboitTargets
    );
    commandList.endRenderPass();

    dispatchAvboitDepthWarp(commandList, avboitTargets);

    renderMaterialPass(
        commandList,
        avboitTargets.lowFramebuffer.get(),
        MaterialPipelinePass::AvboitExtinction,
        true,
        avboitTargets.extinctionBindingSet.get(),
        &avboitTargets
    );
    commandList.endRenderPass();

    dispatchAvboitIntegration(commandList, avboitTargets);

    renderMaterialPass(
        commandList,
        avboitTargets.accumulationFramebuffer.get(),
        MaterialPipelinePass::AvboitAccumulate,
        true,
        avboitTargets.accumulateBindingSet.get(),
        &avboitTargets
    );
    commandList.endRenderPass();
}

void RendererSystem::dispatchAvboitDepthWarp(Core::CommandList& commandList, AvboitFrameTargets& targets){
    __hidden_avboit::DispatchAvboitCompute(
        commandList,
        m_avboitDepthWarpPipeline.get(),
        targets.depthWarpBindingSet.get(),
        targets,
        1u
    );
}

void RendererSystem::dispatchAvboitIntegration(Core::CommandList& commandList, AvboitFrameTargets& targets){
    const u32 pixelCount = targets.lowWidth * targets.lowHeight;
    __hidden_avboit::DispatchAvboitCompute(
        commandList,
        m_avboitIntegratePipeline.get(),
        targets.integrateBindingSet.get(),
        targets,
        DivideUp(pixelCount, static_cast<u32>(NWB_AVBOIT_INTEGRATE_GROUP_SIZE_X))
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

