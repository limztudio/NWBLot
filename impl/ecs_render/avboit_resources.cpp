// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererAvboitSystem::RendererAvboitSystem(RendererSystem& renderer)
    : RendererSystemSubsystemBase(renderer)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_avboit_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename BuildItemsFunc>
static bool CreateBindingLayout(
    Core::GraphicsArena& arena,
    Core::Device& device,
    Core::BindingLayoutHandle& layout,
    const Core::ShaderType::Mask visibility,
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

    return false;
}

static bool CreateComputePipeline(
    Core::Device& device,
    Core::ComputePipelineHandle& pipeline,
    const Core::ShaderHandle& shader,
    const Core::BindingLayoutHandle& bindingLayout
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

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererAvboitSystem::createAvboitResources(){
    auto* device = m_graphics.getDevice();

    if(!ECSRenderDetail::CreateClampSampler(*device, m_deferredState.m_sampler, false)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shared point sampler for AVBOIT"));
        return false;
    }
    if(!ECSRenderDetail::CreateClampSampler(*device, m_avboitState.m_linearSampler, true)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create linear sampler for AVBOIT"));
        return false;
    }

    if(!__hidden_avboit_resources::CreateBindingLayout(
        m_arena,
        *device,
        m_avboitState.m_emptyBindingLayout,
        Core::ShaderType::Pixel,
        [](Core::BindingLayoutDesc&){}
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT empty binding layout"));
        return false;
    }

    if(!__hidden_avboit_resources::CreateBindingLayout(
        m_arena,
        *device,
        m_avboitState.m_occupancyBindingLayout,
        Core::ShaderType::Pixel,
        [](Core::BindingLayoutDesc& bindingLayoutDesc){
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_AVBOIT_BINDING_OPAQUE_DEPTH, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::Sampler(NWB_AVBOIT_BINDING_POINT_SAMPLER, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_AVBOIT_OCCUPANCY_BINDING_COVERAGE_WORDS, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, s_RendererAvboitTransparentDrawPushConstantSize));
        }
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT occupancy binding layout"));
        return false;
    }

    if(!__hidden_avboit_resources::CreateBindingLayout(
        m_arena,
        *device,
        m_avboitState.m_depthWarpBindingLayout,
        Core::ShaderType::Compute,
        [](Core::BindingLayoutDesc& bindingLayoutDesc){
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_AVBOIT_DEPTH_WARP_BINDING_COVERAGE_WORDS, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_AVBOIT_DEPTH_WARP_BINDING_DEPTH_WARP, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_AVBOIT_DEPTH_WARP_BINDING_CONTROL, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(RendererAvboitPushConstants)));
        }
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT depth-warp binding layout"));
        return false;
    }

    if(!__hidden_avboit_resources::CreateBindingLayout(
        m_arena,
        *device,
        m_avboitState.m_extinctionBindingLayout,
        Core::ShaderType::Pixel,
        [](Core::BindingLayoutDesc& bindingLayoutDesc){
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_AVBOIT_BINDING_OPAQUE_DEPTH, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::Sampler(NWB_AVBOIT_BINDING_POINT_SAMPLER, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_AVBOIT_EXTINCTION_BINDING_DEPTH_WARP, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_AVBOIT_EXTINCTION_BINDING_CONTROL, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_AVBOIT_EXTINCTION_BINDING_EXTINCTION, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_AVBOIT_EXTINCTION_BINDING_OVERFLOW_DEPTH, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, s_RendererAvboitTransparentDrawPushConstantSize));
        }
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT extinction binding layout"));
        return false;
    }

    if(!__hidden_avboit_resources::CreateBindingLayout(
        m_arena,
        *device,
        m_avboitState.m_integrateBindingLayout,
        Core::ShaderType::Compute,
        [](Core::BindingLayoutDesc& bindingLayoutDesc){
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_AVBOIT_INTEGRATE_BINDING_EXTINCTION, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_AVBOIT_INTEGRATE_BINDING_TRANSMITTANCE, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_AVBOIT_INTEGRATE_BINDING_CONTROL, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_AVBOIT_INTEGRATE_BINDING_OVERFLOW_DEPTH, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(RendererAvboitPushConstants)));
        }
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT integration binding layout"));
        return false;
    }

    if(!__hidden_avboit_resources::CreateBindingLayout(
        m_arena,
        *device,
        m_avboitState.m_accumulateBindingLayout,
        Core::ShaderType::Pixel,
        [](Core::BindingLayoutDesc& bindingLayoutDesc){
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_AVBOIT_ACCUMULATE_BINDING_DEPTH_WARP, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_AVBOIT_ACCUMULATE_BINDING_TRANSMITTANCE, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_AVBOIT_ACCUMULATE_BINDING_CONTROL, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::Sampler(NWB_AVBOIT_ACCUMULATE_BINDING_LINEAR_SAMPLER, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_SCENE_SHADING_AVBOIT_ACCUMULATE_BINDING, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, s_RendererAvboitTransparentDrawPushConstantSize));
        }
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulation binding layout"));
        return false;
    }

    if(!m_deferredState.m_sceneShadingBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT accumulation requires a scene shading buffer"));
        return false;
    }

    auto loadAvboitShader = [&](
        Core::ShaderHandle& outShader,
        const Name& shaderName,
        const Core::ShaderType::Mask shaderType,
        const Name& debugName
    ) -> bool{
        return m_renderer.shaderSystem().loadShader(
            outShader,
            shaderName,
            Core::ShaderArchive::s_DefaultVariant,
            shaderType,
            debugName
        );
    };

    if(
        !loadAvboitShader(m_avboitState.m_occupancyPixelShader, ECSRenderAvboitDetail::s_AvboitOccupancyPixelShaderName, Core::ShaderType::Pixel, "ECSRender_AvboitOccupancyPS")
        || !loadAvboitShader(m_avboitState.m_depthWarpComputeShader, ECSRenderAvboitDetail::s_AvboitDepthWarpComputeShaderName, Core::ShaderType::Compute, "ECSRender_AvboitDepthWarpCS")
        || !loadAvboitShader(m_avboitState.m_extinctionPixelShader, ECSRenderAvboitDetail::s_AvboitExtinctionPixelShaderName, Core::ShaderType::Pixel, "ECSRender_AvboitExtinctionPS")
        || !loadAvboitShader(m_avboitState.m_integrateComputeShader, ECSRenderAvboitDetail::s_AvboitIntegrateComputeShaderName, Core::ShaderType::Compute, "ECSRender_AvboitIntegrateCS")
        || !loadAvboitShader(m_avboitState.m_accumulatePixelShader, ECSRenderAvboitDetail::s_AvboitAccumulatePixelShaderName, Core::ShaderType::Pixel, "ECSRender_AvboitAccumulatePS")
    )
        return false;

    return true;
}

bool RendererAvboitSystem::createAvboitPipelines(){
    if(!createAvboitResources())
        return false;

    auto* device = m_graphics.getDevice();

    if(!__hidden_avboit_resources::CreateComputePipeline(
        *device,
        m_avboitState.m_depthWarpPipeline,
        m_avboitState.m_depthWarpComputeShader,
        m_avboitState.m_depthWarpBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT depth-warp pipeline"));
        return false;
    }

    if(!__hidden_avboit_resources::CreateComputePipeline(
        *device,
        m_avboitState.m_integratePipeline,
        m_avboitState.m_integrateComputeShader,
        m_avboitState.m_integrateBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT integration pipeline"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

