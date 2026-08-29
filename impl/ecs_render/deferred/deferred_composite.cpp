// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deferred_system.h"

#include <impl/ecs_render/deferred/deferred_graph_private.h>
#include <impl/ecs_render/kernel/renderer_format_private.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/material/renderer_render_state_private.h>
#include <impl/ecs_render/shader/shader_system.h>
#include <impl/ecs_render/shared/renderer_state.h>

#include <core/common/log.h>
#include <core/graphics/module.h>
#include <core/graphics/shader_archive.h>

#include <impl/assets/graphics/deferred/binding_slots.h>
#include <impl/assets/graphics/deferred/names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_deferred_composite{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CompositePushConstants{
    u32 resourceSlots = 0u;
};
static_assert(sizeof(CompositePushConstants) == sizeof(u32));

struct PresentPushConstants{
    u32 resourceSlots = 0u;
    u32 presentationMode = NWB_DEFERRED_PRESENTATION_SDR;
};
static_assert(sizeof(PresentPushConstants) == sizeof(u32) * 2u);


struct DeferredCompositeGraphTask{
    struct Payload{
        RendererDeferredSystem* deferredSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        return ECSRenderDetail::RecordDeferredGraphTask(
            payload,
            commandList,
            [&](RendererDeferredSystem& deferredSystem, DeferredFrameTargets& targets, Core::CommandList& taskCommandList){
                return deferredSystem.renderDeferredComposite(
                    taskCommandList,
                    targets
                );
            }
        );
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererDeferredSystem::createDeferredCompositeResources(){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deferred compositing requires the global descriptor heap"));
        return false;
    }

    if(!m_deferredState.m_compositeComputeBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc
            .setVisibility(Core::ShaderType::Compute)
        ;
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0u, sizeof(__hidden_deferred_composite::CompositePushConstants)));

        m_deferredState.m_compositeComputeBindingLayout = device.createBindingLayout(bindingLayoutDesc);
        if(!m_deferredState.m_compositeComputeBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite-compute binding layout"));
            return false;
        }
    }

    if(!m_deferredState.m_presentBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc
            .setVisibility(Core::ShaderType::Pixel)
        ;
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0u, sizeof(__hidden_deferred_composite::PresentPushConstants)));

        m_deferredState.m_presentBindingLayout = device.createBindingLayout(bindingLayoutDesc);
        if(!m_deferredState.m_presentBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred presentation binding layout"));
            return false;
        }
    }

    if(!ECSRenderDetail::CreateClampSampler(device, m_deferredState.m_sampler, false)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite sampler"));
        return false;
    }

    if(!m_shaderSystem.loadShader(
        m_deferredState.m_compositeComputeShader,
        AssetsGraphicsDeferred::s_CompositeComputeShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_DeferredCompositeCS"
    ))
        return false;

    if(!m_shaderSystem.loadDeferredCompositeVertexShader())
        return false;

    if(!m_shaderSystem.loadShader(
        m_deferredState.m_presentPixelShader,
        AssetsGraphicsDeferred::s_PresentPixelShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Pixel,
        "ECSRender_DeferredPresentPS"
    ))
        return false;

    return true;
}

bool RendererDeferredSystem::createDeferredCompositePipeline(){
    if(!createDeferredCompositeResources())
        return false;

    if(m_deferredState.m_compositeComputePipeline)
        return true;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(m_deferredState.m_compositeComputeShader)
        .addBindingLayout(m_deferredState.m_compositeComputeBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;

    m_deferredState.m_compositeComputePipeline = device.createComputePipeline(pipelineDesc);
    if(!m_deferredState.m_compositeComputePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite-compute pipeline"));
        return false;
    }

    return true;
}

bool RendererDeferredSystem::createDeferredPresentPipeline(Core::Framebuffer* presentationFramebuffer){
    if(!presentationFramebuffer)
        return false;

    if(!createDeferredCompositeResources())
        return false;

    const Core::FramebufferInfo& framebufferInfo = presentationFramebuffer->getFramebufferInfo();
    if(m_deferredState.m_presentPipeline && m_deferredState.m_presentPipeline->getFramebufferInfo() == framebufferInfo)
        return true;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    Core::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc
        .setVertexShader(m_shaderSystem.deferredCompositeVertexShader())
        .setPixelShader(m_deferredState.m_presentPixelShader)
        .setRenderState(ECSRenderDetail::BuildCompositeRenderState())
        .addBindingLayout(m_deferredState.m_presentBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;

    m_deferredState.m_presentPipeline = device.createGraphicsPipeline(pipelineDesc, framebufferInfo);
    if(!m_deferredState.m_presentPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred presentation pipeline"));
        return false;
    }

    return true;
}

Core::GpuTaskId RendererDeferredSystem::declareDeferredCompositeTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    return graph.addTask<__hidden_deferred_composite::DeferredCompositeGraphTask>(
        desc,
        __hidden_deferred_composite::DeferredCompositeGraphTask::Payload{
            .deferredSystem = this,
            .targets = &targets,
            .timingTicket = &timingTicket,
        }
    );
}


bool RendererDeferredSystem::renderDeferredComposite(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets
){
    NWB_ASSERT(m_deferredState.m_compositeComputePipeline);

    // Packet-boundary states for these bindless resources are emitted from the compiled task graph.  This thunk
    // retains only the commands intrinsic to composite recording.

    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_DeferredComposite, m_graphics.getDevice(), commandList);

    Core::ComputeState computeState;
    computeState.setPipeline(m_deferredState.m_compositeComputePipeline.get());
    commandList.setComputeState(computeState);
    m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *m_deferredState.m_compositeComputePipeline);
    const __hidden_deferred_composite::CompositePushConstants pushConstants{
        targets.bindless.slotsBufferDescriptor.slot()
    };
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    const u32 groupCountX = (targets.width + NWB_DEFERRED_COMPOSITE_GROUP_SIZE - 1u) / NWB_DEFERRED_COMPOSITE_GROUP_SIZE;
    const u32 groupCountY = (targets.height + NWB_DEFERRED_COMPOSITE_GROUP_SIZE - 1u) / NWB_DEFERRED_COMPOSITE_GROUP_SIZE;
    commandList.dispatch(groupCountX, groupCountY, 1u);
    return true;
}

bool RendererDeferredSystem::renderDeferredPresent(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const Core::AcquiredPresentationFrame& presentationFrame
){
    NWB_ASSERT(presentationFrame.valid());
    NWB_ASSERT(m_deferredState.m_presentPipeline);
    if(!presentationFrame.valid() || !m_deferredState.m_presentPipeline)
        return false;
    Core::Framebuffer& presentationFramebuffer = *presentationFrame.framebuffer;
    const Core::FramebufferDesc& presentationFramebufferDesc = presentationFramebuffer.getDescription();
    NWB_ASSERT(
        presentationFramebufferDesc.colorAttachments.size() == 1u
        && presentationFramebufferDesc.colorAttachments[0].texture == presentationFrame.backBuffer.texture.get()
        && m_deferredState.m_presentPipeline->getFramebufferInfo() == presentationFramebuffer.getFramebufferInfo()
    );
    if(
        presentationFramebufferDesc.colorAttachments.size() != 1u
        || presentationFramebufferDesc.colorAttachments[0].texture != presentationFrame.backBuffer.texture.get()
        || m_deferredState.m_presentPipeline->getFramebufferInfo() != presentationFramebuffer.getFramebufferInfo()
    )
        return false;

    // The graph owns both the sampled composite transition and this exact acquired texture's render-target state.

    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_DeferredPresent, m_graphics.getDevice(), commandList);

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(presentationFramebuffer.getFramebufferInfo().getViewport());

    Core::GraphicsState graphicsState;
    graphicsState.setPipeline(m_deferredState.m_presentPipeline.get());
    graphicsState.setFramebuffer(&presentationFramebuffer);
    graphicsState.setViewport(viewportState);

    commandList.setGraphicsState(graphicsState);
    m_graphics.getDevice().getDescriptorHeap().bindGraphics(commandList, *m_deferredState.m_presentPipeline);
    const __hidden_deferred_composite::PresentPushConstants pushConstants{
        targets.bindless.slotsBufferDescriptor.slot(),
        m_graphics.isHDR10OutputActive()
            ? NWB_DEFERRED_PRESENTATION_HDR10
            : NWB_DEFERRED_PRESENTATION_SDR
    };
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));

    Core::DrawArguments drawArgs;
    drawArgs.setVertexCount(ECSRenderDetail::s_FullscreenTriangleVertexCount);
    commandList.draw(drawArgs);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

