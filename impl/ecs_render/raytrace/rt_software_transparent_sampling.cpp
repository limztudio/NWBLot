// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "rt_private.h"
#include "renderer_raytracing_state.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureSoftwareTransparentSamplingPipeline(){
    auto& sampling = m_rayTracingState.m_softwareTransparentSampling;
    if(sampling.m_pipelines[0] && sampling.m_pipelines[1])
        return true;
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    if(!sampling.m_layout){
        Core::BindingLayoutDesc desc(m_arena);
        desc.setVisibility(Core::ShaderType::Compute).addItem(Core::BindingLayoutItem::PushConstants(0u, sizeof(SoftwareTransparentSamplingPush)));
        auto layoutResult = device.createBindingLayout(desc);
        if(layoutResult)
            sampling.m_layout = Move(*layoutResult);
        else
            sampling.m_layout.reset();
        if(!layoutResult)
            return false;
    }
    const AStringView variants[] = { "NWB_SW_TRANSPARENT_ADAPTIVE=0", "NWB_SW_TRANSPARENT_ADAPTIVE=1" };
    for(u32 index = 0u; index < LengthOf(variants); ++index){
        if(sampling.m_pipelines[index])
            continue;
        if(!m_shaderSystem.loadShader<ComputeShader>(sampling.m_shaders[index], AssetsGraphicsShadow::s_SwTransparentSoftShaderName, variants[index], "ECSRender_SwShadowTransparentSoft"))
            return false;
        Core::ComputePipelineDesc desc;
        desc.setComputeShader(sampling.m_shaders[index]).addBindingLayout(sampling.m_layout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
        auto pipelinesResult = device.createComputePipeline(desc);
        if(pipelinesResult)
            sampling.m_pipelines[index] = Move(*pipelinesResult);
        else
            sampling.m_pipelines[index].reset();
        if(!pipelinesResult)
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

