// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deferred_system.h"

#include <impl/ecs_render/deferred/deferred_graph_private.h>
#include <impl/ecs_render/deferred/presentation_settings.adhd.h>
#include <impl/ecs_render/kernel/renderer_format_private.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/material/renderer_render_state_private.h>
#include <impl/ecs_render/shader/shader_system.h>
#include <impl/ecs_render/deferred/renderer_deferred_state.h>
#include <impl/ecs_render/kernel/renderer_constants_private.h>
#include <impl/assets/graphics/deferred/binding_slots.h>
#include <impl/assets_graphics/components.adhd.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/shader_archive.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_deferred_composite{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(DeferredCompositePushConstants) == sizeof(u32) * 5u);
static_assert(sizeof(DeferredPresentPushConstants) == sizeof(u32) * 6u);


struct DeferredCompositeGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    // The deferred system, the frame targets, and the timing ticket are required: the pass always records against this frame's targets.
    struct Payload{
        RendererDeferredSystem& deferredSystem;
        DeferredFrameTargets& targets;
        Core::GpuTimingSubmissionTicket& timingTicket;
        ReflectionCompositeInputs reflectionInputs;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
        return ECSRenderDetail::RecordDeferredGraphTask(payload, commandList, [&](RendererDeferredSystem& deferredSystem, DeferredFrameTargets& targets, Core::CommandList& taskCommandList){
            return deferredSystem.renderDeferredComposite(taskCommandList, targets, payload.reflectionInputs);
        });
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererDeferredSystem::createDeferredCompositeResources(){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deferred compositing requires the global descriptor heap"));
        return false;
    }

    if(!m_deferredState.m_compositeComputeBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0u, sizeof(DeferredCompositePushConstants)));

        auto compositeComputeBindingLayoutResult = device.createBindingLayout(bindingLayoutDesc);
        if(compositeComputeBindingLayoutResult)
            m_deferredState.m_compositeComputeBindingLayout = Move(*compositeComputeBindingLayoutResult);
        else
            m_deferredState.m_compositeComputeBindingLayout.reset();
        if(!compositeComputeBindingLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite-compute binding layout"));
            return false;
        }
    }

    if(!m_deferredState.m_presentBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0u, sizeof(DeferredPresentPushConstants)));

        auto presentBindingLayoutResult = device.createBindingLayout(bindingLayoutDesc);
        if(presentBindingLayoutResult)
            m_deferredState.m_presentBindingLayout = Move(*presentBindingLayoutResult);
        else
            m_deferredState.m_presentBindingLayout.reset();
        if(!presentBindingLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred presentation binding layout"));
            return false;
        }
    }

    if(!ECSRenderDetail::CreateClampSampler(device, false, m_deferredState.m_sampler)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite sampler"));
        return false;
    }

    if(!m_shaderSystem.loadShader<ComputeShader>(&DeferredSubsystem::compositeCs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_DeferredCompositeCS", m_deferredState.m_compositeComputeShader))
        return false;

    if(!m_shaderSystem.loadDeferredCompositeVertexShader())
        return false;

    if(!m_shaderSystem.loadShader<PixelShader>(&DeferredSubsystem::compositePs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_DeferredPresentPS", m_deferredState.m_presentPixelShader))
        return false;

    return true;
}

b8 RendererDeferredSystem::createDeferredCompositePipeline(){
    if(!createDeferredCompositeResources())
        return false;

    if(m_deferredState.m_compositeComputePipeline)
        return true;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(m_deferredState.m_compositeComputeShader).addBindingLayout(m_deferredState.m_compositeComputeBindingLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());

    auto compositeComputePipelineResult = device.createComputePipeline(pipelineDesc);
    if(compositeComputePipelineResult)
        m_deferredState.m_compositeComputePipeline = Move(*compositeComputePipelineResult);
    else
        m_deferredState.m_compositeComputePipeline.reset();
    if(!compositeComputePipelineResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite-compute pipeline"));
        return false;
    }

    return true;
}

b8 RendererDeferredSystem::createDeferredPresentPipeline(Core::Framebuffer& presentationFramebuffer){
    if(!createDeferredCompositeResources())
        return false;

    const Core::FramebufferInfo& framebufferInfo = presentationFramebuffer.getFramebufferInfo();
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

    auto presentPipelineResult = device.createGraphicsPipeline(pipelineDesc, framebufferInfo);
    if(presentPipelineResult)
        m_deferredState.m_presentPipeline = Move(*presentPipelineResult);
    else
        m_deferredState.m_presentPipeline.reset();
    if(!presentPipelineResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred presentation pipeline"));
        return false;
    }

    return true;
}

Expected<Core::GpuTaskId> RendererDeferredSystem::declareDeferredCompositeTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    const ReflectionCompositeInputs& reflectionInputs,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    return graph.addTask<__hidden_deferred_composite::DeferredCompositeGraphTask>(
        desc,
        __hidden_deferred_composite::DeferredCompositeGraphTask::Payload{ .deferredSystem = *this, .targets = targets, .timingTicket = timingTicket, .reflectionInputs = reflectionInputs, }
    );
}


b8 RendererDeferredSystem::renderDeferredComposite(Core::CommandList& commandList, DeferredFrameTargets& targets, const ReflectionCompositeInputs& reflectionInputs){
    NWB_ASSERT(m_deferredState.m_compositeComputePipeline);

    // Boundary states are graph-owned; this thunk holds only composite commands.

    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_DeferredComposite, m_graphics.getDevice(), commandList);

    Core::ComputeState computeState;
    computeState.setPipeline(m_deferredState.m_compositeComputePipeline.get());
    commandList.setComputeState(computeState);
    m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *m_deferredState.m_compositeComputePipeline);
    const DeferredCompositePushConstants pushConstants = ResolveDeferredCompositePushConstants(
        targets.bindless.slotsBufferDescriptor.slot(),
        targets.avboit.refractionFramebuffer != nullptr,
        reflectionInputs
    );
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    const u32 groupCountX = (targets.width + NWB_DEFERRED_COMPOSITE_GROUP_SIZE - 1u) / NWB_DEFERRED_COMPOSITE_GROUP_SIZE;
    const u32 groupCountY = (targets.height + NWB_DEFERRED_COMPOSITE_GROUP_SIZE - 1u) / NWB_DEFERRED_COMPOSITE_GROUP_SIZE;
    commandList.dispatch(groupCountX, groupCountY, 1u);
    return true;
}

b8 RendererDeferredSystem::renderDeferredPresent(Core::CommandList& commandList, DeferredFrameTargets& targets, const Core::AcquiredPresentationFrame& presentationFrame, const Core::GpuDescriptorHandle outputLayerImage){
    NWB_ASSERT(presentationFrame.valid());
    NWB_ASSERT(m_deferredState.m_presentPipeline);
    Core::Framebuffer& presentationFramebuffer = *presentationFrame.framebuffer;
    NWB_ASSERT(
        presentationFramebuffer.getDescription().colorAttachments.size() == 1u
        && presentationFramebuffer.getDescription().colorAttachments[0].texture == presentationFrame.backBuffer.texture.get()
        && m_deferredState.m_presentPipeline->getFramebufferInfo() == presentationFramebuffer.getFramebufferInfo()
    );

    // Graph owns the composite transition and the acquired texture's render-target state.

    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_DeferredPresent, m_graphics.getDevice(), commandList);

    // Discard avoids a tile restore; STORE keeps the image for UI and present.
    Core::RenderPassParameters renderPassParameters;
    renderPassParameters.colorAttachmentActions[0u].loadAction = Core::RenderPassLoadAction::Discard;
    renderPassParameters.colorAttachmentActions[0u].storeAction = Core::RenderPassStoreAction::Store;
    commandList.beginRenderPass(presentationFramebuffer, renderPassParameters);
    if(!commandList.isRenderPassActive())
        return false;

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(presentationFramebuffer.getFramebufferInfo().getViewport());

    Core::GraphicsState graphicsState;
    graphicsState.setPipeline(m_deferredState.m_presentPipeline.get());
    graphicsState.setFramebuffer(&presentationFramebuffer);
    graphicsState.setViewport(viewportState);

    commandList.setGraphicsState(graphicsState);
    m_graphics.getDevice().getDescriptorHeap().bindGraphics(commandList, *m_deferredState.m_presentPipeline);
    const DeferredPresentPushConstants pushConstants = ResolveDeferredPresentPushConstants(
        targets.bindless.slotsBufferDescriptor.slot(),
        m_graphics.isHDR10OutputActive(),
        m_deferredState.m_presentationSettings,
        outputLayerImage.valid() ? outputLayerImage.slot() : Core::GpuDescriptorHandle::s_Invalid
    );
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));

    Core::DrawArguments drawArgs;
    drawArgs.setVertexCount(s_FullscreenTriangleVertexCount);
    commandList.draw(drawArgs);
    commandList.endRenderPass();
    return !commandList.isRenderPassActive();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

