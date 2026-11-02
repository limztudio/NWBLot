// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/kernel/compute_pipeline_utils.h>
#include <impl/ecs_render/kernel/renderer_format_private.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/shader/shader_system.h>
#include <impl/assets_graphics/components.adhd.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/shader_archive.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererAvboitSystem::createAvboitSampler(){
    auto& device = m_graphics.getDevice();

    if(!ECSRenderDetail::CreateClampSampler(device, true, m_avboitState.m_linearSampler)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create linear sampler for AVBOIT"));
        return false;
    }

    return true;
}

b8 RendererAvboitSystem::createAvboitResources(){
    if(!createAvboitSampler())
        return false;

    auto loadAvboitComputeShader = [&](const Core::Assets::AssetRef<ComputeShader> TransparentSubsystem::* member, const Name& debugName, Core::ShaderHandle& outShader) -> b8{
        return m_shaderSystem.loadShader<ComputeShader>(member, Core::ShaderArchive::s_DefaultVariant, debugName, outShader);
    };

    if(
        !loadAvboitComputeShader(&TransparentSubsystem::avboitDepthWarpCs, "ECSRender_AvboitDepthWarpCS", m_avboitState.m_depthWarpComputeShader)
        || !loadAvboitComputeShader(&TransparentSubsystem::avboitIntegrateCs, "ECSRender_AvboitIntegrateCS", m_avboitState.m_integrateComputeShader)
    )
        return false;

    return true;
}

b8 RendererAvboitSystem::createAvboitPipelines(){
    if(!createAvboitResources())
        return false;

    auto& device = m_graphics.getDevice();
    const auto materialPassBindingLayout = m_materialSystem.prepareMaterialPassBindingLayout();
    if(!materialPassBindingLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT requires the shared material-pass push-constant layout"));
        return false;
    }

    if(!m_avboitState.m_depthWarpPipeline && !RendererComputePipelineDetail::CreateHeapComputePipeline(device, m_avboitState.m_depthWarpComputeShader, *materialPassBindingLayout, false, m_avboitState.m_depthWarpPipeline)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT depth-warp pipeline"));
        return false;
    }

    if(!m_avboitState.m_integratePipeline && !RendererComputePipelineDetail::CreateHeapComputePipeline(device, m_avboitState.m_integrateComputeShader, *materialPassBindingLayout, false, m_avboitState.m_integratePipeline)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT integration pipeline"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

