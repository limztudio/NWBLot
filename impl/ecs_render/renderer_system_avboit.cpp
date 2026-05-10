// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_avboit.h"

#include <core/common/log.h>
#include <core/graphics/graphics.h>
#include <core/graphics/shader_archive.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_renderer_avboit{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Core::TextureSubresourceSet s_FramebufferSubresources = Core::TextureSubresourceSet(0, 1, 0, 1);
static constexpr u32 s_AvboitDownsample = 4u;
static constexpr u32 s_AvboitVirtualSlices = 128u;
static constexpr u32 s_AvboitPhysicalSlices = 64u;
static constexpr u32 s_AvboitExtinctionSlicesPerWord = 4u;
static constexpr f32 s_AvboitExtinctionFixedScale = 45.985905f;
static constexpr f32 s_AvboitSelfOcclusionSliceBias = 2.f;
static constexpr usize s_AvboitControlWordCount = 8u;

static constexpr Name s_AvboitOccupancyPixelShaderName("engine/graphics/avboit_occupancy_ps");
static constexpr Name s_AvboitDepthWarpComputeShaderName("engine/graphics/avboit_depth_warp_cs");
static constexpr Name s_AvboitExtinctionPixelShaderName("engine/graphics/avboit_extinction_ps");
static constexpr Name s_AvboitIntegrateComputeShaderName("engine/graphics/avboit_integrate_cs");
static constexpr Name s_AvboitAccumulatePixelShaderName("engine/graphics/avboit_accumulate_ps");


template<usize N>
static Core::Format::Enum SelectSupportedFormat(
    Core::IDevice& device,
    const Core::Format::Enum (&candidates)[N],
    const Core::FormatSupport::Mask requiredSupport
){
    for(const Core::Format::Enum format : candidates){
        if((device.queryFormatSupport(format) & requiredSupport) == requiredSupport)
            return format;
    }

    return Core::Format::UNKNOWN;
}

static bool EnsurePointClampSampler(Core::IDevice& device, Core::SamplerHandle& sampler, const tchar* failureMessage){
    if(sampler)
        return true;

    Core::SamplerDesc samplerDesc;
    samplerDesc
        .setAllFilters(false)
        .setAllAddressModes(Core::SamplerAddressMode::Clamp)
    ;
    sampler = device.createSampler(samplerDesc);
    if(sampler)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{}"), failureMessage);
    return false;
}

static bool EnsureLinearClampSampler(Core::IDevice& device, Core::SamplerHandle& sampler, const tchar* failureMessage){
    if(sampler)
        return true;

    Core::SamplerDesc samplerDesc;
    samplerDesc
        .setAllFilters(true)
        .setAllAddressModes(Core::SamplerAddressMode::Clamp)
    ;
    sampler = device.createSampler(samplerDesc);
    if(sampler)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{}"), failureMessage);
    return false;
}

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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::Format::Enum SelectRendererAvboitAccumColorFormat(Core::IDevice& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::RGBA16_FLOAT,
        Core::Format::RGBA8_UNORM,
    };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget | Core::FormatSupport::Blendable;

    return __hidden_renderer_avboit::SelectSupportedFormat(device, candidates, requiredSupport);
}

Core::Format::Enum SelectRendererAvboitAccumExtinctionFormat(Core::IDevice& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::R16_FLOAT,
        Core::Format::R32_FLOAT,
        Core::Format::RGBA16_FLOAT,
        Core::Format::R8_UNORM,
        Core::Format::RGBA8_UNORM,
    };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget | Core::FormatSupport::Blendable;

    return __hidden_renderer_avboit::SelectSupportedFormat(device, candidates, requiredSupport);
}

Core::Format::Enum SelectRendererAvboitTransmittanceFormat(Core::IDevice& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::R16_FLOAT,
    };
    constexpr Core::FormatSupport::Mask requiredSupport =
        Core::FormatSupport::Texture
        | Core::FormatSupport::ShaderSample
        | Core::FormatSupport::ShaderUavStore
    ;

    return __hidden_renderer_avboit::SelectSupportedFormat(device, candidates, requiredSupport);
}

Core::Format::Enum SelectRendererAvboitLowRasterFormat(Core::IDevice& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::R8_UNORM,
        Core::Format::RGBA8_UNORM,
    };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget;

    return __hidden_renderer_avboit::SelectSupportedFormat(device, candidates, requiredSupport);
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
        .setRenderTarget(0, __hidden_renderer_avboit::BuildAdditiveBlendTarget())
        .setRenderTarget(1, __hidden_renderer_avboit::BuildAdditiveBlendTarget(Core::ColorMask::Red))
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

RendererAvboitPushConstants BuildRendererAvboitPushConstants(const RendererSystem::AvboitFrameTargets& targets, const f32 alpha){
    RendererAvboitPushConstants pushConstants;
    pushConstants.frame[0] = targets.fullWidth;
    pushConstants.frame[1] = targets.fullHeight;
    pushConstants.frame[2] = targets.lowWidth;
    pushConstants.frame[3] = targets.lowHeight;
    pushConstants.volume[0] = targets.virtualSliceCount;
    pushConstants.volume[1] = targets.physicalSliceCount;
    const u32 physicalExtinctionWordCount = DivideUp(targets.physicalSliceCount, __hidden_renderer_avboit::s_AvboitExtinctionSlicesPerWord);
    pushConstants.volume[2] = static_cast<u32>(
        static_cast<u64>(targets.lowWidth) * static_cast<u64>(targets.lowHeight) * static_cast<u64>(physicalExtinctionWordCount)
    );
    pushConstants.volume[3] = DivideUp(targets.virtualSliceCount, 32u);
    pushConstants.params = Float4(
        alpha,
        __hidden_renderer_avboit::s_AvboitExtinctionFixedScale,
        __hidden_renderer_avboit::s_AvboitSelfOcclusionSliceBias,
        0.f
    );
    return pushConstants;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererSystem::ensureAvboitResources(){
    Core::IDevice* device = m_graphics.getDevice();

    if(!__hidden_renderer_avboit::EnsurePointClampSampler(*device, m_deferredSampler, NWB_TEXT("RendererSystem: failed to create shared point sampler for AVBOIT")))
        return false;
    if(!__hidden_renderer_avboit::EnsureLinearClampSampler(*device, m_avboitLinearSampler, NWB_TEXT("RendererSystem: failed to create linear sampler for AVBOIT")))
        return false;

    if(!m_avboitEmptyBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);

        m_avboitEmptyBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_avboitEmptyBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT empty binding layout"));
            return false;
        }
    }

    if(!m_avboitOccupancyBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Sampler(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(2, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, s_RendererAvboitTransparentDrawPushConstantSize));

        m_avboitOccupancyBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_avboitOccupancyBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT occupancy binding layout"));
            return false;
        }
    }

    if(!m_avboitDepthWarpBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(2, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(RendererAvboitPushConstants)));

        m_avboitDepthWarpBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_avboitDepthWarpBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT depth-warp binding layout"));
            return false;
        }
    }

    if(!m_avboitExtinctionBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Sampler(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(2, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(3, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(4, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(5, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, s_RendererAvboitTransparentDrawPushConstantSize));

        m_avboitExtinctionBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_avboitExtinctionBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT extinction binding layout"));
            return false;
        }
    }

    if(!m_avboitIntegrateBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(2, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(3, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(RendererAvboitPushConstants)));

        m_avboitIntegrateBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_avboitIntegrateBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT integration binding layout"));
            return false;
        }
    }

    if(!m_avboitAccumulateBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(2, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Sampler(3, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(4, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, s_RendererAvboitTransparentDrawPushConstantSize));

        m_avboitAccumulateBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_avboitAccumulateBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulation binding layout"));
            return false;
        }
    }

    if(!m_sceneShadingBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT accumulation requires a scene shading buffer"));
        return false;
    }

    if(!ensureShaderLoaded(
        m_avboitOccupancyPixelShader,
        __hidden_renderer_avboit::s_AvboitOccupancyPixelShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Pixel,
        "ECSRender_AvboitOccupancyPS"
    ))
        return false;

    if(!ensureShaderLoaded(
        m_avboitDepthWarpComputeShader,
        __hidden_renderer_avboit::s_AvboitDepthWarpComputeShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_AvboitDepthWarpCS"
    ))
        return false;

    if(!ensureShaderLoaded(
        m_avboitExtinctionPixelShader,
        __hidden_renderer_avboit::s_AvboitExtinctionPixelShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Pixel,
        "ECSRender_AvboitExtinctionPS"
    ))
        return false;

    if(!ensureShaderLoaded(
        m_avboitIntegrateComputeShader,
        __hidden_renderer_avboit::s_AvboitIntegrateComputeShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_AvboitIntegrateCS"
    ))
        return false;

    if(!ensureShaderLoaded(
        m_avboitAccumulatePixelShader,
        __hidden_renderer_avboit::s_AvboitAccumulatePixelShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Pixel,
        "ECSRender_AvboitAccumulatePS"
    ))
        return false;

    return true;
}

bool RendererSystem::ensureAvboitPipelines(AvboitFrameTargets& targets){
    if(!ensureAvboitResources())
        return false;

    Core::IDevice* device = m_graphics.getDevice();

    if(!m_avboitDepthWarpPipeline){
        Core::ComputePipelineDesc pipelineDesc;
        pipelineDesc
            .setComputeShader(m_avboitDepthWarpComputeShader)
            .addBindingLayout(m_avboitDepthWarpBindingLayout)
        ;
        m_avboitDepthWarpPipeline = device->createComputePipeline(pipelineDesc);
        if(!m_avboitDepthWarpPipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT depth-warp pipeline"));
            return false;
        }
    }

    if(!m_avboitIntegratePipeline){
        Core::ComputePipelineDesc pipelineDesc;
        pipelineDesc
            .setComputeShader(m_avboitIntegrateComputeShader)
            .addBindingLayout(m_avboitIntegrateBindingLayout)
        ;
        m_avboitIntegratePipeline = device->createComputePipeline(pipelineDesc);
        if(!m_avboitIntegratePipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT integration pipeline"));
            return false;
        }
    }

    return targets.valid();
}

bool RendererSystem::createAvboitFrameTargets(
    DeferredFrameTargets& createdTargets,
    const Core::Format::Enum lowRasterFormat,
    const Core::Format::Enum accumColorFormat,
    const Core::Format::Enum accumExtinctionFormat,
    const Core::Format::Enum transmittanceFormat
){
    Core::IDevice* device = m_graphics.getDevice();
    AvboitFrameTargets avboitTargets;
    avboitTargets.fullWidth = createdTargets.width;
    avboitTargets.fullHeight = createdTargets.height;
    const u64 lowWidth = Max<u64>(
        1u,
        DivideUp(static_cast<u64>(createdTargets.width), static_cast<u64>(__hidden_renderer_avboit::s_AvboitDownsample))
    );
    const u64 lowHeight = Max<u64>(
        1u,
        DivideUp(static_cast<u64>(createdTargets.height), static_cast<u64>(__hidden_renderer_avboit::s_AvboitDownsample))
    );
    if(lowWidth > Limit<u32>::s_Max || lowHeight > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT low-resolution dimensions exceed u32 limits"));
        return false;
    }
    avboitTargets.lowWidth = static_cast<u32>(lowWidth);
    avboitTargets.lowHeight = static_cast<u32>(lowHeight);
    avboitTargets.virtualSliceCount = __hidden_renderer_avboit::s_AvboitVirtualSlices;
    avboitTargets.physicalSliceCount = __hidden_renderer_avboit::s_AvboitPhysicalSlices;
    avboitTargets.lowRasterFormat = lowRasterFormat;
    avboitTargets.accumColorFormat = accumColorFormat;
    avboitTargets.accumExtinctionFormat = accumExtinctionFormat;
    avboitTargets.transmittanceFormat = transmittanceFormat;

    Core::TextureDesc lowRasterDesc;
    lowRasterDesc
        .setWidth(avboitTargets.lowWidth)
        .setHeight(avboitTargets.lowHeight)
        .setFormat(avboitTargets.lowRasterFormat)
        .setInRenderTarget(true)
        .setName("engine/avboit/low_raster")
        .setClearValue(Core::Color(0.f, 0.f, 0.f, 0.f))
    ;
    avboitTargets.lowRasterTarget = m_graphics.createTexture(lowRasterDesc);
    if(!avboitTargets.lowRasterTarget){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT low-resolution raster target"));
        return false;
    }

    Core::TextureDesc accumColorDesc;
    accumColorDesc
        .setWidth(avboitTargets.fullWidth)
        .setHeight(avboitTargets.fullHeight)
        .setFormat(avboitTargets.accumColorFormat)
        .setInRenderTarget(true)
        .setName("engine/avboit/accum_color")
        .setClearValue(Core::Color(0.f, 0.f, 0.f, 0.f))
    ;
    avboitTargets.accumColor = m_graphics.createTexture(accumColorDesc);
    if(!avboitTargets.accumColor){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulated color target"));
        return false;
    }

    Core::TextureDesc accumExtinctionDesc;
    accumExtinctionDesc
        .setWidth(avboitTargets.fullWidth)
        .setHeight(avboitTargets.fullHeight)
        .setFormat(avboitTargets.accumExtinctionFormat)
        .setInRenderTarget(true)
        .setName("engine/avboit/accum_extinction")
        .setClearValue(Core::Color(0.f, 0.f, 0.f, 0.f))
    ;
    avboitTargets.accumExtinction = m_graphics.createTexture(accumExtinctionDesc);
    if(!avboitTargets.accumExtinction){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulated extinction target"));
        return false;
    }

    Core::FramebufferDesc lowFramebufferDesc;
    lowFramebufferDesc.addColorAttachment(avboitTargets.lowRasterTarget.get(), __hidden_renderer_avboit::s_FramebufferSubresources);
    avboitTargets.lowFramebuffer = device->createFramebuffer(lowFramebufferDesc);
    if(!avboitTargets.lowFramebuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT low-resolution framebuffer"));
        return false;
    }

    Core::FramebufferDesc accumulationFramebufferDesc;
    accumulationFramebufferDesc
        .addColorAttachment(avboitTargets.accumColor.get(), __hidden_renderer_avboit::s_FramebufferSubresources)
        .addColorAttachment(avboitTargets.accumExtinction.get(), __hidden_renderer_avboit::s_FramebufferSubresources)
        .setDepthAttachment(
            Core::FramebufferAttachment()
                .setTexture(createdTargets.depth.get())
                .setSubresources(__hidden_renderer_avboit::s_FramebufferSubresources)
                .setReadOnly(true)
        )
    ;
    avboitTargets.accumulationFramebuffer = device->createFramebuffer(accumulationFramebufferDesc);
    if(!avboitTargets.accumulationFramebuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulation framebuffer"));
        return false;
    }

    const u32 coverageWordCount = DivideUp(avboitTargets.virtualSliceCount, 32u);
    const u64 coverageBytes = static_cast<u64>(coverageWordCount) * sizeof(u32);
    const u64 depthWarpBytes = static_cast<u64>(avboitTargets.virtualSliceCount) * sizeof(u32);
    const u64 lowPixelCount = static_cast<u64>(avboitTargets.lowWidth) * avboitTargets.lowHeight;
    if(lowPixelCount > static_cast<u64>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT low-resolution pixel count exceeds u32 limits"));
        return false;
    }
    const u32 physicalExtinctionWordCount = DivideUp(
        avboitTargets.physicalSliceCount,
        __hidden_renderer_avboit::s_AvboitExtinctionSlicesPerWord
    );
    if(physicalExtinctionWordCount == 0 || lowPixelCount > static_cast<u64>(Limit<u32>::s_Max) / physicalExtinctionWordCount){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT packed extinction word count exceeds u32 limits"));
        return false;
    }
    const u64 extinctionWordCount = lowPixelCount * physicalExtinctionWordCount;
    const u64 extinctionBytes = extinctionWordCount * sizeof(u32);
    const u64 extinctionOverflowBytes = lowPixelCount * sizeof(u32);

    Core::BufferDesc coverageDesc;
    coverageDesc
        .setByteSize(coverageBytes)
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName("engine/avboit/depth_coverage")
    ;
    avboitTargets.coverageBuffer = m_graphics.createBuffer(coverageDesc);
    if(!avboitTargets.coverageBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT coverage buffer"));
        return false;
    }

    Core::BufferDesc depthWarpDesc;
    depthWarpDesc
        .setByteSize(depthWarpBytes)
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName("engine/avboit/depth_warp_lut")
    ;
    avboitTargets.depthWarpBuffer = m_graphics.createBuffer(depthWarpDesc);
    if(!avboitTargets.depthWarpBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT depth warp buffer"));
        return false;
    }

    Core::BufferDesc controlDesc;
    controlDesc
        .setByteSize(static_cast<u64>(__hidden_renderer_avboit::s_AvboitControlWordCount) * sizeof(u32))
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName("engine/avboit/control")
    ;
    avboitTargets.controlBuffer = m_graphics.createBuffer(controlDesc);
    if(!avboitTargets.controlBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT control buffer"));
        return false;
    }

    Core::BufferDesc extinctionDesc;
    extinctionDesc
        .setByteSize(extinctionBytes)
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName("engine/avboit/packed_extinction_volume")
    ;
    avboitTargets.extinctionBuffer = m_graphics.createBuffer(extinctionDesc);
    if(!avboitTargets.extinctionBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT extinction volume"));
        return false;
    }

    Core::BufferDesc extinctionOverflowDesc;
    extinctionOverflowDesc
        .setByteSize(extinctionOverflowBytes)
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName("engine/avboit/extinction_overflow_depth")
    ;
    avboitTargets.extinctionOverflowBuffer = m_graphics.createBuffer(extinctionOverflowDesc);
    if(!avboitTargets.extinctionOverflowBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT extinction overflow buffer"));
        return false;
    }

    Core::TextureDesc transmittanceDesc;
    transmittanceDesc
        .setWidth(avboitTargets.lowWidth)
        .setHeight(avboitTargets.lowHeight)
        .setDepth(avboitTargets.physicalSliceCount)
        .setFormat(avboitTargets.transmittanceFormat)
        .setDimension(Core::TextureDimension::Texture3D)
        .setInUAV(true)
        .setName("engine/avboit/transmittance_volume")
        .setClearValue(Core::Color(1.f, 1.f, 1.f, 1.f))
    ;
    avboitTargets.transmittanceTexture = m_graphics.createTexture(transmittanceDesc);
    if(!avboitTargets.transmittanceTexture){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT transmittance volume"));
        return false;
    }

    Core::BindingSetDesc occupancyBindingSetDesc;
    occupancyBindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        0,
        createdTargets.depth.get(),
        createdTargets.depthFormat,
        __hidden_renderer_avboit::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    occupancyBindingSetDesc.addItem(Core::BindingSetItem::Sampler(1, m_deferredSampler.get()));
    occupancyBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(2, avboitTargets.coverageBuffer.get()));
    avboitTargets.occupancyBindingSet = device->createBindingSet(occupancyBindingSetDesc, m_avboitOccupancyBindingLayout);
    if(!avboitTargets.occupancyBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT occupancy binding set"));
        return false;
    }

    Core::BindingSetDesc depthWarpBindingSetDesc;
    depthWarpBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(0, avboitTargets.coverageBuffer.get()));
    depthWarpBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(1, avboitTargets.depthWarpBuffer.get()));
    depthWarpBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(2, avboitTargets.controlBuffer.get()));
    avboitTargets.depthWarpBindingSet = device->createBindingSet(depthWarpBindingSetDesc, m_avboitDepthWarpBindingLayout);
    if(!avboitTargets.depthWarpBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT depth-warp binding set"));
        return false;
    }

    Core::BindingSetDesc extinctionBindingSetDesc;
    extinctionBindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        0,
        createdTargets.depth.get(),
        createdTargets.depthFormat,
        __hidden_renderer_avboit::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    extinctionBindingSetDesc.addItem(Core::BindingSetItem::Sampler(1, m_deferredSampler.get()));
    extinctionBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(2, avboitTargets.depthWarpBuffer.get()));
    extinctionBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(3, avboitTargets.controlBuffer.get()));
    extinctionBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(4, avboitTargets.extinctionBuffer.get()));
    extinctionBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(5, avboitTargets.extinctionOverflowBuffer.get()));
    avboitTargets.extinctionBindingSet = device->createBindingSet(extinctionBindingSetDesc, m_avboitExtinctionBindingLayout);
    if(!avboitTargets.extinctionBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT extinction binding set"));
        return false;
    }

    Core::BindingSetDesc integrateBindingSetDesc;
    integrateBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(0, avboitTargets.extinctionBuffer.get()));
    integrateBindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
        1,
        avboitTargets.transmittanceTexture.get(),
        avboitTargets.transmittanceFormat,
        __hidden_renderer_avboit::s_FramebufferSubresources,
        Core::TextureDimension::Texture3D
    ));
    integrateBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(2, avboitTargets.controlBuffer.get()));
    integrateBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(3, avboitTargets.extinctionOverflowBuffer.get()));
    avboitTargets.integrateBindingSet = device->createBindingSet(integrateBindingSetDesc, m_avboitIntegrateBindingLayout);
    if(!avboitTargets.integrateBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT integration binding set"));
        return false;
    }

    Core::BindingSetDesc accumulateBindingSetDesc;
    accumulateBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(0, avboitTargets.depthWarpBuffer.get()));
    accumulateBindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        1,
        avboitTargets.transmittanceTexture.get(),
        avboitTargets.transmittanceFormat,
        __hidden_renderer_avboit::s_FramebufferSubresources,
        Core::TextureDimension::Texture3D
    ));
    accumulateBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(2, avboitTargets.controlBuffer.get()));
    accumulateBindingSetDesc.addItem(Core::BindingSetItem::Sampler(3, m_avboitLinearSampler.get()));
    accumulateBindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(4, m_sceneShadingBuffer.get()));
    avboitTargets.accumulateBindingSet = device->createBindingSet(accumulateBindingSetDesc, m_avboitAccumulateBindingLayout);
    if(!avboitTargets.accumulateBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulation binding set"));
        return false;
    }

    createdTargets.avboit = Move(avboitTargets);
    return true;
}

void RendererSystem::clearAvboitTargets(Core::ICommandList& commandList, AvboitFrameTargets& targets){
    if(targets.lowRasterTarget){
        commandList.setTextureState(targets.lowRasterTarget.get(), __hidden_renderer_avboit::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    }

    if(targets.accumColor){
        commandList.setTextureState(targets.accumColor.get(), __hidden_renderer_avboit::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    }

    if(targets.accumExtinction){
        commandList.setTextureState(targets.accumExtinction.get(), __hidden_renderer_avboit::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    }

    if(targets.coverageBuffer){
        commandList.setBufferState(targets.coverageBuffer.get(), Core::ResourceStates::CopyDest);
    }

    if(targets.depthWarpBuffer){
        commandList.setBufferState(targets.depthWarpBuffer.get(), Core::ResourceStates::CopyDest);
    }

    if(targets.controlBuffer){
        commandList.setBufferState(targets.controlBuffer.get(), Core::ResourceStates::CopyDest);
    }

    if(targets.extinctionBuffer){
        commandList.setBufferState(targets.extinctionBuffer.get(), Core::ResourceStates::CopyDest);
    }

    if(targets.extinctionOverflowBuffer){
        commandList.setBufferState(targets.extinctionOverflowBuffer.get(), Core::ResourceStates::CopyDest);
    }

    if(targets.transmittanceTexture){
        commandList.setTextureState(targets.transmittanceTexture.get(), __hidden_renderer_avboit::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    }

    commandList.commitBarriers();

    if(targets.lowRasterTarget){
        commandList.clearTextureFloat(targets.lowRasterTarget.get(), __hidden_renderer_avboit::s_FramebufferSubresources, Core::Color(0.f, 0.f, 0.f, 0.f));
    }

    if(targets.accumColor){
        commandList.clearTextureFloat(targets.accumColor.get(), __hidden_renderer_avboit::s_FramebufferSubresources, Core::Color(0.f, 0.f, 0.f, 0.f));
    }

    if(targets.accumExtinction){
        commandList.clearTextureFloat(targets.accumExtinction.get(), __hidden_renderer_avboit::s_FramebufferSubresources, Core::Color(0.f, 0.f, 0.f, 0.f));
    }

    if(targets.coverageBuffer){
        commandList.clearBufferUInt(targets.coverageBuffer.get(), 0u);
    }

    if(targets.depthWarpBuffer){
        commandList.clearBufferUInt(targets.depthWarpBuffer.get(), 0u);
    }

    if(targets.controlBuffer){
        commandList.clearBufferUInt(targets.controlBuffer.get(), 0u);
    }

    if(targets.extinctionBuffer){
        commandList.clearBufferUInt(targets.extinctionBuffer.get(), 0u);
    }

    if(targets.extinctionOverflowBuffer){
        commandList.clearBufferUInt(targets.extinctionOverflowBuffer.get(), Limit<u32>::s_Max);
    }

    if(targets.transmittanceTexture){
        commandList.clearTextureFloat(targets.transmittanceTexture.get(), __hidden_renderer_avboit::s_FramebufferSubresources, Core::Color(1.f, 1.f, 1.f, 1.f));
    }
}

void RendererSystem::renderAvboitPasses(Core::ICommandList& commandList, DeferredFrameTargets& targets){
    AvboitFrameTargets& avboitTargets = targets.avboit;
    if(!avboitTargets.valid())
        return;
    if(!ensureAvboitPipelines(avboitTargets))
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

void RendererSystem::dispatchAvboitDepthWarp(Core::ICommandList& commandList, AvboitFrameTargets& targets){
    if(!m_avboitDepthWarpPipeline || !targets.depthWarpBindingSet)
        return;

    commandList.setResourceStatesForBindingSet(targets.depthWarpBindingSet.get());
    commandList.commitBarriers();

    Core::ComputeState computeState;
    computeState.setPipeline(m_avboitDepthWarpPipeline.get());
    computeState.addBindingSet(targets.depthWarpBindingSet.get());
    commandList.setComputeState(computeState);

    const RendererAvboitPushConstants pushConstants = BuildRendererAvboitPushConstants(targets, 1.f);
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.dispatch(1, 1, 1);
}

void RendererSystem::dispatchAvboitIntegration(Core::ICommandList& commandList, AvboitFrameTargets& targets){
    if(!m_avboitIntegratePipeline || !targets.integrateBindingSet)
        return;

    commandList.setResourceStatesForBindingSet(targets.integrateBindingSet.get());
    commandList.commitBarriers();

    Core::ComputeState computeState;
    computeState.setPipeline(m_avboitIntegratePipeline.get());
    computeState.addBindingSet(targets.integrateBindingSet.get());
    commandList.setComputeState(computeState);

    const RendererAvboitPushConstants pushConstants = BuildRendererAvboitPushConstants(targets, 1.f);
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));

    const u32 pixelCount = targets.lowWidth * targets.lowHeight;
    commandList.dispatch(DivideUp(pixelCount, 64u), 1, 1);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

