// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/csg/csg_interval_private.h>

#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/material/renderer_render_state_private.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/shader/shader_system.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>
#include <impl/ecs_render/shared/renderer_state.h>

#include <core/common/log.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/module.h>
#include <core/graphics/shader_archive.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CsgIntervalDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool CreateCsgIntervalBindingLayout(
    Core::GraphicsArena& arena,
    Core::Device& device,
    Core::BindingLayoutHandle& layout,
    Core::ShaderType::Mask visibility
){
    if(layout)
        return true;

    Core::BindingLayoutDesc bindingLayoutDesc(arena);
    bindingLayoutDesc.setVisibility(visibility);
    // All CSG images and selector payloads are global-heap resources.  The standalone dispatch only needs this
    // existing push range to select its CSG context UniformBuffer and mesh view.
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CsgIntervalDispatchPushConstants)));

    layout = device.createBindingLayout(bindingLayoutDesc);
    return layout != nullptr;
}

[[nodiscard]] static bool CreateIntervalPeelPipeline(
    Core::Device& device,
    Core::ComputePipelineHandle& pipeline,
    const Core::ShaderHandle& shader,
    const Core::BindingLayoutHandle& intervalBindingLayout
){
    if(pipeline)
        return true;

    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(shader)
        .addBindingLayout(intervalBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;

    pipeline = device.createComputePipeline(pipelineDesc);
    return pipeline != nullptr;
}

[[nodiscard]] static bool CreateReceiverSpanBuildPipeline(
    Core::Device& device,
    Core::ComputePipelineHandle& pipeline,
    const Core::ShaderHandle& shader,
    const Core::BindingLayoutHandle& receiverSpanBuildBindingLayout
){
    if(pipeline)
        return true;

    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(shader)
        .addBindingLayout(receiverSpanBuildBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;

    pipeline = device.createComputePipeline(pipelineDesc);
    return pipeline != nullptr;
}

[[nodiscard]] static bool CreateIntervalCombinePipeline(
    Core::Device& device,
    Core::ComputePipelineHandle& pipeline,
    const Core::ShaderHandle& shader,
    const Core::BindingLayoutHandle& intervalCombineBindingLayout
){
    if(pipeline)
        return true;

    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(shader)
        .addBindingLayout(intervalCombineBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;

    pipeline = device.createComputePipeline(pipelineDesc);
    return pipeline != nullptr;
}

[[nodiscard]] static bool CreateIntervalCapFillPipeline(
    Core::Device& device,
    Core::GraphicsPipelineHandle& pipeline,
    const Core::ShaderHandle& vertexShader,
    const Core::ShaderHandle& pixelShader,
    const Core::BindingLayoutHandle& clipBindingLayout,
    const Core::FramebufferInfo& framebufferInfo
){
    if(pipeline && pipeline->getFramebufferInfo() == framebufferInfo)
        return true;

    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    Core::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc
        .setVertexShader(vertexShader)
        .setPixelShader(pixelShader)
        .setRenderState(ECSRenderDetail::BuildCompositeRenderState())
        .addBindingLayout(clipBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;

    pipeline = device.createGraphicsPipeline(pipelineDesc, framebufferInfo);
    return pipeline != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererCsgSystem::createCsgIntervalPeelResources(DeferredFrameTargets& targets, const bool capFillRequired){
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

    if(!CsgIntervalDetail::CreateCsgIntervalBindingLayout(
        m_arena,
        device,
        m_csgState.m_intervalPeelBindingLayout,
        Core::ShaderType::Compute
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval peel binding layout"));
        return false;
    }

    if(!CsgIntervalDetail::CreateCsgIntervalBindingLayout(
        m_arena,
        device,
        m_csgState.m_receiverSpanBuildBindingLayout,
        Core::ShaderType::Compute
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG receiver span build binding layout"));
        return false;
    }

    if(!CsgIntervalDetail::CreateCsgIntervalBindingLayout(
        m_arena,
        device,
        m_csgState.m_intervalCombineBindingLayout,
        Core::ShaderType::Compute
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval combine binding layout"));
        return false;
    }

    if(!m_csgState.m_intervalPeelComputeShader){
        if(!m_shaderSystem.loadShader(
            m_csgState.m_intervalPeelComputeShader,
            AssetsGraphicsCsg::s_IntervalPeelComputeShaderName,
            Core::ShaderArchive::s_DefaultVariant,
            Core::ShaderType::Compute,
            "ECSRender_CsgIntervalPeelCS"
        ))
            return false;
    }

    if(!m_csgState.m_receiverSpanBuildComputeShader){
        if(!m_shaderSystem.loadShader(
            m_csgState.m_receiverSpanBuildComputeShader,
            AssetsGraphicsCsg::s_ReceiverSpanBuildComputeShaderName,
            Core::ShaderArchive::s_DefaultVariant,
            Core::ShaderType::Compute,
            "ECSRender_CsgReceiverSpanBuildCS"
        ))
            return false;
    }

    if(!m_csgState.m_intervalCombineComputeShader){
        if(!m_shaderSystem.loadShader(
            m_csgState.m_intervalCombineComputeShader,
            AssetsGraphicsCsg::s_IntervalCombineComputeShaderName,
            Core::ShaderArchive::s_DefaultVariant,
            Core::ShaderType::Compute,
            "ECSRender_CsgIntervalCombineCS"
        ))
            return false;
    }

    if(capFillRequired){
        if(!m_shaderSystem.loadDeferredCompositeVertexShader())
            return false;

        if(!m_csgState.m_intervalCapFillPixelShader){
            if(!m_shaderSystem.loadShader(
                m_csgState.m_intervalCapFillPixelShader,
                AssetsGraphicsCsg::s_IntervalCapFillPixelShaderName,
                Core::ShaderArchive::s_DefaultVariant,
                Core::ShaderType::Pixel,
                "ECSRender_CsgIntervalCapFillPS"
            ))
                return false;
        }
    }

    if(!CsgIntervalDetail::CreateIntervalPeelPipeline(
        device,
        m_csgState.m_intervalPeelPipeline,
        m_csgState.m_intervalPeelComputeShader,
        m_csgState.m_intervalPeelBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval peel pipeline"));
        return false;
    }

    if(!CsgIntervalDetail::CreateReceiverSpanBuildPipeline(
        device,
        m_csgState.m_receiverSpanBuildPipeline,
        m_csgState.m_receiverSpanBuildComputeShader,
        m_csgState.m_receiverSpanBuildBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG receiver span build pipeline"));
        return false;
    }

    if(!CsgIntervalDetail::CreateIntervalCombinePipeline(
        device,
        m_csgState.m_intervalCombinePipeline,
        m_csgState.m_intervalCombineComputeShader,
        m_csgState.m_intervalCombineBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval combine pipeline"));
        return false;
    }

    if(capFillRequired && !CsgIntervalDetail::CreateIntervalCapFillPipeline(
        device,
        m_csgState.m_intervalCapFillPipeline,
        m_shaderSystem.deferredCompositeVertexShader(),
        m_csgState.m_intervalCapFillPixelShader,
        m_csgState.m_clipBindingLayout,
        targets.framebuffer->getFramebufferInfo()
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval cap fill pipeline"));
        return false;
    }

    if(!createCsgIntervalSampleResources(targets))
        return false;

    return true;
}

bool RendererCsgSystem::createCsgIntervalSampleResources(DeferredFrameTargets& targets){
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

bool RendererCsgSystem::createCsgIntervalSampleStateBuffer(){
    if(m_csgState.m_intervalSampleStateBuffer)
        return true;

    Core::BufferDesc bufferDesc;
    bufferDesc
        .setByteSize(sizeof(CsgIntervalSampleStateGpuData))
        .setIsConstantBuffer(true)
        .setDebugName("engine/csg/interval_sample_state")
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;

    m_csgState.m_intervalSampleStateBuffer = m_graphics.createBuffer(bufferDesc);
    if(!m_csgState.m_intervalSampleStateBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create CSG interval sample state buffer"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

