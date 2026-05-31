// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_avboit_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename BuildItemsFunc>
static bool CreateBindingLayout(
    Core::GraphicsArena& arena,
    Core::Device& device,
    Core::BindingLayoutHandle& layout,
    const Core::ShaderType::Mask visibility,
    const tchar* failureMessage,
    const BuildItemsFunc& buildItems
){
    if(layout)
        return true;

    Core::BindingLayoutDesc bindingLayoutDesc(arena);
    bindingLayoutDesc.setVisibility(visibility);
    buildItems(bindingLayoutDesc);

    layout = device.createBindingLayout(bindingLayoutDesc);
    if(layout)
        return true;

    NWB_LOGGER_ERROR(failureMessage);
    return false;
}

static bool CreateComputePipeline(
    Core::Device& device,
    Core::ComputePipelineHandle& pipeline,
    const Core::ShaderHandle& shader,
    const Core::BindingLayoutHandle& bindingLayout,
    const tchar* failureMessage
){
    if(pipeline)
        return true;

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(shader)
        .addBindingLayout(bindingLayout)
    ;
    pipeline = device.createComputePipeline(pipelineDesc);
    if(pipeline)
        return true;

    NWB_LOGGER_ERROR(failureMessage);
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererSystem::createAvboitResources(){
    auto* device = m_graphics.getDevice();

    if(!ECSRenderDetail::CreatePointClampSampler(*device, m_deferredSampler, NWB_TEXT("RendererSystem: failed to create shared point sampler for AVBOIT")))
        return false;
    if(!ECSRenderDetail::CreateClampSampler(
        *device,
        m_avboitLinearSampler,
        true,
        NWB_TEXT("RendererSystem: failed to create linear sampler for AVBOIT")
    ))
        return false;

    if(!__hidden_avboit_resources::CreateBindingLayout(
        m_arena,
        *device,
        m_avboitEmptyBindingLayout,
        Core::ShaderType::Pixel,
        NWB_TEXT("RendererSystem: failed to create AVBOIT empty binding layout"),
        [](Core::BindingLayoutDesc&){}
    ))
        return false;

    if(!__hidden_avboit_resources::CreateBindingLayout(
        m_arena,
        *device,
        m_avboitOccupancyBindingLayout,
        Core::ShaderType::Pixel,
        NWB_TEXT("RendererSystem: failed to create AVBOIT occupancy binding layout"),
        [](Core::BindingLayoutDesc& bindingLayoutDesc){
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(0, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::Sampler(1, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(2, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, s_RendererAvboitTransparentDrawPushConstantSize));
        }
    ))
        return false;

    if(!__hidden_avboit_resources::CreateBindingLayout(
        m_arena,
        *device,
        m_avboitDepthWarpBindingLayout,
        Core::ShaderType::Compute,
        NWB_TEXT("RendererSystem: failed to create AVBOIT depth-warp binding layout"),
        [](Core::BindingLayoutDesc& bindingLayoutDesc){
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(0, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(1, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(2, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(RendererAvboitPushConstants)));
        }
    ))
        return false;

    if(!__hidden_avboit_resources::CreateBindingLayout(
        m_arena,
        *device,
        m_avboitExtinctionBindingLayout,
        Core::ShaderType::Pixel,
        NWB_TEXT("RendererSystem: failed to create AVBOIT extinction binding layout"),
        [](Core::BindingLayoutDesc& bindingLayoutDesc){
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(0, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::Sampler(1, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(2, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(3, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(4, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(5, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, s_RendererAvboitTransparentDrawPushConstantSize));
        }
    ))
        return false;

    if(!__hidden_avboit_resources::CreateBindingLayout(
        m_arena,
        *device,
        m_avboitIntegrateBindingLayout,
        Core::ShaderType::Compute,
        NWB_TEXT("RendererSystem: failed to create AVBOIT integration binding layout"),
        [](Core::BindingLayoutDesc& bindingLayoutDesc){
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(0, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(1, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(2, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(3, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(RendererAvboitPushConstants)));
        }
    ))
        return false;

    if(!__hidden_avboit_resources::CreateBindingLayout(
        m_arena,
        *device,
        m_avboitAccumulateBindingLayout,
        Core::ShaderType::Pixel,
        NWB_TEXT("RendererSystem: failed to create AVBOIT accumulation binding layout"),
        [](Core::BindingLayoutDesc& bindingLayoutDesc){
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(0, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(1, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(2, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::Sampler(3, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(4, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, s_RendererAvboitTransparentDrawPushConstantSize));
        }
    ))
        return false;

    if(!m_sceneShadingBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT accumulation requires a scene shading buffer"));
        return false;
    }

    auto loadAvboitShader = [&](
        Core::ShaderHandle& outShader,
        const Name& shaderName,
        const Core::ShaderType::Mask shaderType,
        const Name& debugName
    ) -> bool{
        return loadShader(
            outShader,
            shaderName,
            Core::ShaderArchive::s_DefaultVariant,
            shaderType,
            debugName
        );
    };

    if(
        !loadAvboitShader(m_avboitOccupancyPixelShader, ECSRenderAvboitDetail::s_AvboitOccupancyPixelShaderName, Core::ShaderType::Pixel, "ECSRender_AvboitOccupancyPS")
        || !loadAvboitShader(m_avboitDepthWarpComputeShader, ECSRenderAvboitDetail::s_AvboitDepthWarpComputeShaderName, Core::ShaderType::Compute, "ECSRender_AvboitDepthWarpCS")
        || !loadAvboitShader(m_avboitExtinctionPixelShader, ECSRenderAvboitDetail::s_AvboitExtinctionPixelShaderName, Core::ShaderType::Pixel, "ECSRender_AvboitExtinctionPS")
        || !loadAvboitShader(m_avboitIntegrateComputeShader, ECSRenderAvboitDetail::s_AvboitIntegrateComputeShaderName, Core::ShaderType::Compute, "ECSRender_AvboitIntegrateCS")
        || !loadAvboitShader(m_avboitAccumulatePixelShader, ECSRenderAvboitDetail::s_AvboitAccumulatePixelShaderName, Core::ShaderType::Pixel, "ECSRender_AvboitAccumulatePS")
    )
        return false;

    return true;
}

bool RendererSystem::createAvboitPipelines(){
    if(!createAvboitResources())
        return false;

    auto* device = m_graphics.getDevice();

    return
        __hidden_avboit_resources::CreateComputePipeline(
            *device,
            m_avboitDepthWarpPipeline,
            m_avboitDepthWarpComputeShader,
            m_avboitDepthWarpBindingLayout,
            NWB_TEXT("RendererSystem: failed to create AVBOIT depth-warp pipeline")
        )
        && __hidden_avboit_resources::CreateComputePipeline(
            *device,
            m_avboitIntegratePipeline,
            m_avboitIntegrateComputeShader,
            m_avboitIntegrateBindingLayout,
            NWB_TEXT("RendererSystem: failed to create AVBOIT integration pipeline")
        )
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

