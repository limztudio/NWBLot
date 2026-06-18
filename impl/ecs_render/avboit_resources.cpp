// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_private.h"

#include <core/graphics/pipeline_helpers.h>


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

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererAvboitSystem::createAvboitResources(){
    auto* device = graphics().getDevice();

    if(!ECSRenderDetail::CreateClampSampler(*device, deferredState().m_sampler, false)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shared point sampler for AVBOIT"));
        return false;
    }
    if(!ECSRenderDetail::CreateClampSampler(*device, avboitState().m_linearSampler, true)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create linear sampler for AVBOIT"));
        return false;
    }

    if(!__hidden_avboit_resources::CreateBindingLayout(
        arena(),
        *device,
        avboitState().m_emptyBindingLayout,
        Core::ShaderType::Pixel,
        [](Core::BindingLayoutDesc&){}
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT empty binding layout"));
        return false;
    }

    if(!__hidden_avboit_resources::CreateBindingLayout(
        arena(),
        *device,
        avboitState().m_occupancyBindingLayout,
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
        arena(),
        *device,
        avboitState().m_depthWarpBindingLayout,
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
        arena(),
        *device,
        avboitState().m_extinctionBindingLayout,
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
        arena(),
        *device,
        avboitState().m_integrateBindingLayout,
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
        arena(),
        *device,
        avboitState().m_accumulateBindingLayout,
        Core::ShaderType::Pixel,
        [](Core::BindingLayoutDesc& bindingLayoutDesc){
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_AVBOIT_ACCUMULATE_BINDING_DEPTH_WARP, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_AVBOIT_ACCUMULATE_BINDING_TRANSMITTANCE, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_AVBOIT_ACCUMULATE_BINDING_CONTROL, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::Sampler(NWB_AVBOIT_ACCUMULATE_BINDING_LINEAR_SAMPLER, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_AVBOIT_ACCUMULATE_BINDING_SCENE_SHADING, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_AVBOIT_ACCUMULATE_BINDING_LIGHT_LIST, 1));
            bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, s_RendererAvboitTransparentDrawPushConstantSize));
        }
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulation binding layout"));
        return false;
    }

    if(!deferredState().m_sceneShadingBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT accumulation requires a scene shading buffer"));
        return false;
    }

    if(!deferredState().m_lightBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT accumulation requires a scene light buffer"));
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
        !loadAvboitShader(avboitState().m_occupancyPixelShader, AssetsGraphicsAvboit::s_OccupancyPixelShaderName, Core::ShaderType::Pixel, "ECSRender_AvboitOccupancyPS")
        || !loadAvboitShader(avboitState().m_depthWarpComputeShader, AssetsGraphicsAvboit::s_DepthWarpComputeShaderName, Core::ShaderType::Compute, "ECSRender_AvboitDepthWarpCS")
        || !loadAvboitShader(avboitState().m_extinctionPixelShader, AssetsGraphicsAvboit::s_ExtinctionPixelShaderName, Core::ShaderType::Pixel, "ECSRender_AvboitExtinctionPS")
        || !loadAvboitShader(avboitState().m_integrateComputeShader, AssetsGraphicsAvboit::s_IntegrateComputeShaderName, Core::ShaderType::Compute, "ECSRender_AvboitIntegrateCS")
        || !loadAvboitShader(avboitState().m_accumulatePixelShader, AssetsGraphicsAvboit::s_AccumulatePixelShaderName, Core::ShaderType::Pixel, "ECSRender_AvboitAccumulatePS")
    )
        return false;

    return true;
}

bool RendererAvboitSystem::createAvboitPipelines(){
    if(!createAvboitResources())
        return false;

    auto* device = graphics().getDevice();

    if(!Core::CreateComputePipelineIfNeeded(
        *device,
        avboitState().m_depthWarpPipeline,
        avboitState().m_depthWarpComputeShader,
        avboitState().m_depthWarpBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT depth-warp pipeline"));
        return false;
    }

    if(!Core::CreateComputePipelineIfNeeded(
        *device,
        avboitState().m_integratePipeline,
        avboitState().m_integrateComputeShader,
        avboitState().m_integrateBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT integration pipeline"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

