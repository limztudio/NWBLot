// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/kernel/compute_pipeline_utils.h>
#include <impl/ecs_render/material/renderer_render_state_private.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/shader/shader_system.h>
#include <impl/ecs_render/deferred/deferred_frame_targets.h>
#include <impl/assets_graphics/components.adhd.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/shader_archive.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_csg_interval_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 CreateIntervalCapFillPipeline(
    Core::Device& device,
    const Core::ShaderHandle& vertexShader,
    const Core::ShaderHandle& pixelShader,
    const Core::BindingLayoutHandle& clipBindingLayout,
    const Core::FramebufferInfo& framebufferInfo,
    Core::GraphicsPipelineHandle& pipeline
){
    if(pipeline && pipeline->getFramebufferInfo() == framebufferInfo)
        return true;

    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    Core::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc.setVertexShader(vertexShader).setPixelShader(pixelShader).setRenderState(ECSRenderDetail::BuildCompositeRenderState()).addBindingLayout(clipBindingLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());

    auto pipelineResult = device.createGraphicsPipeline(pipelineDesc, framebufferInfo);
    if(pipelineResult)
        pipeline = Move(*pipelineResult);
    else
        pipeline.reset();
    return static_cast<b8>(pipelineResult);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererCsgSystem::createCsgIntervalPeelResources(DeferredFrameTargets& targets, const b8 capFillRequired){
    auto& device = m_graphics.getDevice();
    if(!createCsgClipResources())
        return false;
    if(!m_csgState.m_clipBindingLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG interval peel requires a CSG clip binding layout"));
        return false;
    }
    if(!m_meshSystem.meshViewBufferSnapshot().valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG interval peel requires a mesh view buffer"));
        return false;
    }
    if(
        !targets.csgCapBackNormal
        || !targets.csgIntervalDepth
        || !targets.csgIntervalId
        || !targets.csgReceiverEventData
        || !targets.csgReceiverEventCount
        || !targets.csgReceiverSpanData
        || !targets.csgReceiverSpanCount
        || !targets.csgRemovedIntervalDepth
        || !targets.csgRemovedIntervalCapNormal
        || !targets.csgRemovedIntervalData
        || !targets.csgRemovedIntervalCount
        || targets.csgPeelLayerCount == 0u
        || targets.csgReceiverEventLayerCount == 0u
        || targets.csgReceiverSpanLayerCount == 0u
        || targets.csgRemovedIntervalLayerCount == 0u
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG interval peel requires valid peel targets"));
        return false;
    }

    if(!createCsgIntervalSampleStateBuffer())
        return false;

    // Push range selects the CSG context and mesh view; the rest is heap-owned.
    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(CsgIntervalDispatchPushConstants), Core::ShaderType::Compute, m_csgState.m_intervalPeelBindingLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval peel binding layout"));
        return false;
    }

    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(CsgIntervalDispatchPushConstants), Core::ShaderType::Compute, m_csgState.m_receiverSpanBuildBindingLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG receiver span build binding layout"));
        return false;
    }

    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(CsgIntervalDispatchPushConstants), Core::ShaderType::Compute, m_csgState.m_intervalCombineBindingLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval combine binding layout"));
        return false;
    }

    if(!m_csgState.m_intervalPeelComputeShader){
        if(!m_shaderSystem.loadShader<ComputeShader>(&CsgSubsystem::intervalPeelCs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_CsgIntervalPeelCS", m_csgState.m_intervalPeelComputeShader))
            return false;
    }

    if(!m_csgState.m_receiverSpanBuildComputeShader){
        if(!m_shaderSystem.loadShader<ComputeShader>(&CsgSubsystem::receiverSpanBuildCs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_CsgReceiverSpanBuildCS", m_csgState.m_receiverSpanBuildComputeShader))
            return false;
    }

    if(!m_csgState.m_intervalCombineComputeShader){
        if(!m_shaderSystem.loadShader<ComputeShader>(&CsgSubsystem::intervalCombineCs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_CsgIntervalCombineCS", m_csgState.m_intervalCombineComputeShader))
            return false;
    }

    if(capFillRequired){
        if(!m_shaderSystem.loadDeferredCompositeVertexShader())
            return false;

        if(!m_csgState.m_intervalCapFillPixelShader){
            if(!m_shaderSystem.loadShader<PixelShader>(&CsgSubsystem::intervalCapFillPs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_CsgIntervalCapFillPS", m_csgState.m_intervalCapFillPixelShader))
                return false;
        }
    }

    if(!m_csgState.m_intervalPeelPipeline && !RendererComputePipelineDetail::CreateHeapComputePipeline(device, m_csgState.m_intervalPeelComputeShader, m_csgState.m_intervalPeelBindingLayout, false, m_csgState.m_intervalPeelPipeline)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval peel pipeline"));
        return false;
    }

    if(!m_csgState.m_receiverSpanBuildPipeline && !RendererComputePipelineDetail::CreateHeapComputePipeline(device, m_csgState.m_receiverSpanBuildComputeShader, m_csgState.m_receiverSpanBuildBindingLayout, false, m_csgState.m_receiverSpanBuildPipeline)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG receiver span build pipeline"));
        return false;
    }

    if(!m_csgState.m_intervalCombinePipeline && !RendererComputePipelineDetail::CreateHeapComputePipeline(device, m_csgState.m_intervalCombineComputeShader, m_csgState.m_intervalCombineBindingLayout, false, m_csgState.m_intervalCombinePipeline)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval combine pipeline"));
        return false;
    }

    if(capFillRequired && !__hidden_csg_interval_resources::CreateIntervalCapFillPipeline(
        device,
        m_shaderSystem.deferredCompositeVertexShader(),
        m_csgState.m_intervalCapFillPixelShader,
        m_csgState.m_clipBindingLayout,
        targets.framebuffer->getFramebufferInfo(),
        m_csgState.m_intervalCapFillPipeline
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval cap fill pipeline"));
        return false;
    }

    if(!createCsgIntervalSampleResources(targets))
        return false;

    return true;
}

b8 RendererCsgSystem::createCsgIntervalSampleResources(DeferredFrameTargets& targets){
    if(!m_meshSystem.meshViewBufferSnapshot().valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG interval sampling requires a mesh view buffer"));
        return false;
    }
    if(
        !targets.csgReceiverEventData
        || !targets.csgReceiverEventCount
        || !targets.csgRemovedIntervalDepth
        || !targets.csgRemovedIntervalCapNormal
        || !targets.csgRemovedIntervalData
        || !targets.csgRemovedIntervalCount
        || targets.csgReceiverEventLayerCount == 0u
        || targets.csgRemovedIntervalLayerCount == 0u
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG interval sampling requires valid peel targets"));
        return false;
    }

    if(!createCsgIntervalSampleStateBuffer())
        return false;

    return true;
}

b8 RendererCsgSystem::prepareRefractionIntervalCapResources(const DeferredFrameTargets& targets){
    if(!targets.avboit.refractionFramebuffer || !m_csgState.m_clipBindingLayout)
        return false;
    if(!m_shaderSystem.loadDeferredCompositeVertexShader())
        return false;
    if(!m_csgState.m_refractionCapFillPixelShader){
        if(!m_shaderSystem.loadShader<PixelShader>(&CsgSubsystem::refractionCapFillPs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_CsgRefractionCapFillPS", m_csgState.m_refractionCapFillPixelShader))
            return false;
    }
    if(!__hidden_csg_interval_resources::CreateIntervalCapFillPipeline(
        m_graphics.getDevice(),
        m_shaderSystem.deferredCompositeVertexShader(),
        m_csgState.m_refractionCapFillPixelShader,
        m_csgState.m_clipBindingLayout,
        targets.avboit.refractionFramebuffer->getFramebufferInfo(),
        m_csgState.m_refractionCapFillPipeline
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG refraction cap fill pipeline"));
        return false;
    }
    return true;
}

b8 RendererCsgSystem::createCsgIntervalSampleStateBuffer(){
    if(m_csgState.m_intervalSampleStateBuffer)
        return true;

    Core::BufferDesc bufferDesc;
    bufferDesc
        .setByteSize(sizeof(CsgIntervalSampleStateGpuData))
        .setIsConstantBuffer(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName("engine/csg/interval_sample_state")
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;

    auto intervalSampleStateBufferResult = m_graphics.createBuffer(bufferDesc);
    if(intervalSampleStateBufferResult)
        m_csgState.m_intervalSampleStateBuffer = Move(*intervalSampleStateBufferResult);
    else
        m_csgState.m_intervalSampleStateBuffer.reset();
    if(!intervalSampleStateBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval sample state buffer"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

