// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_internal.h"

#include <impl/assets_graphics/ui/names.h>
#include <impl/assets_shader/loader.h>

#include <core/graphics/shader_archive.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_gpu_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TargetReadyTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements{ Core::GpuQueueCapability::Graphics, true };
    struct Payload{
        GpuVersion<GpuTargetVersion> target;
        Core::GpuGraphResourceId color;
    };


public:
    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commands, const Core::GpuTaskRecordContext& context)noexcept{
        return context.declarations.textureForResource(payload.color) == payload.target->m_color.get() && !commands.commandRecordingFailed();
    }
};

[[nodiscard]] static Expected<Core::GpuTaskId> DeclareTargetInitialization(void* const rawTarget, Core::GpuTaskGraph& graph){
    const auto& target = *static_cast<GpuVersion<GpuTargetVersion>*>(rawTarget);
    const auto colorResult = graph.importTexture(
        target->m_color,
        Core::GpuGraphResourceDesc().setIdentity(Name("ui.target_initialization")).setMarkerLabel("UI Target Initialization")
            .setType(Core::GpuGraphResourceType::Texture).setInitialState(Core::ResourceStates::Unknown)
            .setExternalFinalState(Core::ResourceStates::ShaderResource)
    );
    if(!colorResult)
        return MakeUnexpected(Failure{});
    const Core::GpuGraphResourceId color = *colorResult;
    const auto clearResult = graph.addClearTextureTask(
        Core::GpuTaskDesc().setIdentity(Name("ui.target_initialization_clear")).setMarkerLabel("UI Target Initialization Clear"),
        Core::GpuClearTextureTaskDesc{ .destination = color, .valueType = Core::GpuClearTextureTaskValueType::Float, .floatValue = Core::Color(0.0f, 0.0f, 0.0f, 0.0f), }
    );
    if(!clearResult)
        return MakeUnexpected(Failure{});
    const Core::GpuTaskId clear = *clearResult;
    const Core::GpuTaskResourceUse use{ color, {}, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read };
    const auto readyResult = graph.addTask<TargetReadyTask>(
        Core::GpuTaskDesc().setIdentity(Name("ui.target_initialization_ready")).setMarkerLabel("UI Target Ready").setDependencies(&clear, 1u).setResourceUses(&use, 1u),
        TargetReadyTask::Payload{ target, color }
    );
    if(!readyResult)
        return MakeUnexpected(Failure{});
    const Core::GpuTaskId ready = *readyResult;
    return ready;
}

[[nodiscard]] static Core::RenderState PaintRenderState()noexcept{
    Core::RenderState state;
    state.depthStencilState.disableDepthTest().disableDepthWrite();
    state.rasterState.enableDepthClip().setCullNone();
    state.blendState.targets[NWB_UI_COLOR_TARGET_LOCATION]
        .enableBlend()
        .setSrcBlend(Core::BlendFactor::One)
        .setDestBlend(Core::BlendFactor::InvSrcAlpha)
        .setBlendOp(Core::BlendOp::Add)
        .setSrcBlendAlpha(Core::BlendFactor::One)
        .setDestBlendAlpha(Core::BlendFactor::InvSrcAlpha)
        .setBlendOpAlpha(Core::BlendOp::Add)
    ;
    return state;
}

template<typename TShader>
[[nodiscard]] static bool LoadShader(GpuRendererState& state, Core::ShaderHandle& shader, const Name& identity){
    return ShaderAssetLoader::Load<TShader>(shader, identity, Core::ShaderArchive::s_DefaultVariant, identity, state.m_graphics, state.m_assets, state.m_resolver, NWB_TEXT("GpuRenderer"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuRendererState::MatchesAcquired(const Core::AcquiredPresentationFrame& first, const Core::AcquiredPresentationFrame& second)noexcept{
    const Core::QueueSubmissionToken& a = first.backBuffer.availabilityCompletion;
    const Core::QueueSubmissionToken& b = second.backBuffer.availabilityCompletion;
    return
        first.valid() && second.valid()
        && first.backBuffer.texture == second.backBuffer.texture && first.framebuffer == second.framebuffer
        && first.backBuffer.index == second.backBuffer.index && first.backBuffer.nativeInitialState == second.backBuffer.nativeInitialState
        && a.value == b.value && a.physicalQueueIndex == b.physicalQueueIndex
        && a.deviceGeneration == b.deviceGeneration && a.queue == b.queue
    ;
}

bool GpuRendererState::createResources(){
    if(m_resources)
        return true;
    Core::Device& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    auto resourcesResult = MakeGpuVersion<GpuSharedResources>(m_arena, m_graphics);
    if(!resourcesResult)
        return false;
    GpuVersion<GpuSharedResources> resources = Move(*resourcesResult);
    if(
        !__hidden_ui_gpu_resources::LoadShader<VertexShader>(*this, resources->m_vertexShader, AssetsGraphicsUi::s_VertexShaderName)
        || !__hidden_ui_gpu_resources::LoadShader<PixelShader>(*this, resources->m_pixelShader, AssetsGraphicsUi::s_PixelShaderName)
        || !__hidden_ui_gpu_resources::LoadShader<VertexShader>(*this, resources->m_outputVertexShader, AssetsGraphicsUi::s_OutputVertexShaderName)
        || !__hidden_ui_gpu_resources::LoadShader<PixelShader>(*this, resources->m_outputPixelShader, AssetsGraphicsUi::s_OutputPixelShaderName)
    )
        return false;
    Core::VertexAttributeDesc attributes[NWB_UI_VERTEX_ATTRIBUTE_COUNT];
    attributes[NWB_UI_VERTEX_POSITION_LOCATION].setFormat(Core::Format::RG32_FLOAT).setBufferIndex(NWB_UI_VERTEX_BUFFER_INDEX).setOffset(NWB_UI_VERTEX_POSITION_BYTE_OFFSET).setElementStride(sizeof(Vertex)).setName(Name("POSITION"));
    attributes[NWB_UI_VERTEX_UV_LOCATION].setFormat(Core::Format::RG32_FLOAT).setBufferIndex(NWB_UI_VERTEX_BUFFER_INDEX).setOffset(NWB_UI_VERTEX_UV_BYTE_OFFSET).setElementStride(sizeof(Vertex)).setName(Name("TEXCOORD"));
    attributes[NWB_UI_VERTEX_COLOR_LOCATION].setFormat(Core::Format::RGBA32_FLOAT).setBufferIndex(NWB_UI_VERTEX_BUFFER_INDEX).setOffset(NWB_UI_VERTEX_COLOR_BYTE_OFFSET).setElementStride(sizeof(Vertex)).setName(Name("COLOR"));
    auto inputLayoutResult = device.createInputLayout(attributes, NWB_UI_VERTEX_ATTRIBUTE_COUNT, resources->m_vertexShader.get());
    if(inputLayoutResult)
        resources->m_inputLayout = Move(*inputLayoutResult);
    else
        resources->m_inputLayout.reset();
    if(!inputLayoutResult)
        return false;
    Core::BindingLayoutDesc paintLayout(m_arena);
    paintLayout.setVisibility(Core::ShaderType::AllGraphics).addItem(Core::BindingLayoutItem::PushConstants(0u, sizeof(GpuPaintPushConstants)));
    auto layoutResult = device.createBindingLayout(paintLayout);
    if(layoutResult)
        resources->m_layout = Move(*layoutResult);
    else
        resources->m_layout.reset();
    Core::BindingLayoutDesc outputLayout(m_arena);
    outputLayout.setVisibility(Core::ShaderType::AllGraphics).addItem(Core::BindingLayoutItem::PushConstants(0u, sizeof(GpuOutputPushConstants)));
    auto outputLayoutResult = device.createBindingLayout(outputLayout);
    if(outputLayoutResult)
        resources->m_outputLayout = Move(*outputLayoutResult);
    else
        resources->m_outputLayout.reset();
    Core::SamplerDesc sampler;
    sampler.setAllFilters(true).setAllAddressModes(Core::SamplerAddressMode::Clamp);
    auto samplerResult = device.createSampler(sampler);
    if(samplerResult)
        resources->m_sampler = Move(*samplerResult);
    else
        resources->m_sampler.reset();
    if(!layoutResult || !outputLayoutResult || !samplerResult)
        return false;
    const Expected<Core::GpuDescriptorHandle> samplerDescriptor = heap.allocate(Core::GpuDescriptorClass::Sampler);
    if(!samplerDescriptor)
        return false;
    resources->m_samplerDescriptor = *samplerDescriptor;
    if(!heap.write(resources->m_samplerDescriptor, Core::DescriptorWriteItem::Sampler(0u, resources->m_sampler.get())))
        return false;
    Core::GraphicsPipelineDesc pipeline;
    pipeline
        .setInputLayout(resources->m_inputLayout)
        .setVertexShader(resources->m_vertexShader)
        .setPixelShader(resources->m_pixelShader)
        .setRenderState(__hidden_ui_gpu_resources::PaintRenderState())
        .addBindingLayout(resources->m_layout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    auto pipelineResult = device.createGraphicsPipeline(pipeline, Core::FramebufferInfo().addColorFormat(Core::Format::RGBA16_FLOAT));
    if(pipelineResult)
        resources->m_pipeline = Move(*pipelineResult);
    else
        resources->m_pipeline.reset();
    if(!pipelineResult)
        return false;
    m_resources = Move(resources);
    return true;
}

Expected<GpuVersion<GpuTargetVersion>> GpuRendererState::createTarget(const u32 width, const u32 height){
    auto targetResult = MakeGpuVersion<GpuTargetVersion>(m_arena, m_graphics);
    if(!targetResult)
        return MakeUnexpected(targetResult.error());
    GpuVersion<GpuTargetVersion> target = Move(*targetResult);
    Core::TextureDesc description;
    description
        .setName(Name("ui.layer"))
        .setWidth(width).setHeight(height)
        .setFormat(Core::Format::RGBA16_FLOAT)
        .setInRenderTarget(true)
        .setInitialState(Core::ResourceStates::ShaderResource)
        .setKeepInitialState(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
    ;
    Core::Device& device = m_graphics.getDevice();
    auto colorResult = device.createTexture(description);
    if(colorResult)
        target->m_color = Move(*colorResult);
    else
        target->m_color.reset();
    if(!colorResult)
        return MakeUnexpected(Failure{});
    auto framebufferResult = device.createFramebuffer(Core::FramebufferDesc().addColorAttachment(target->m_color.get()));
    if(framebufferResult)
        target->m_framebuffer = Move(*framebufferResult);
    else
        target->m_framebuffer.reset();
    if(!framebufferResult)
        return MakeUnexpected(Failure{});
    const Core::GpuPhysicalQueueId queue = device.getPrimaryPhysicalQueue(Core::CommandQueue::Graphics);
    // A creation descriptor does not transition a fresh native image. Initialize before publishing its bindless view.
    if(!queue.valid())
        return MakeUnexpected(Failure{});
    const auto submission = m_graphics.submitStandaloneTaskGraph(&target, __hidden_ui_gpu_resources::DeclareTargetInitialization, queue);
    if(!submission)
        return MakeUnexpected(submission.error());
    target->m_readinessToken = *submission;
    if(!target->m_readinessToken.valid() || !target->m_readinessToken.hasPhysicalQueueIdentity())
        return MakeUnexpected(Failure{});
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    const Expected<Core::GpuDescriptorHandle> sampledImage = heap.allocate(Core::GpuDescriptorClass::SampledImage);
    if(!sampledImage)
        return MakeUnexpected(sampledImage.error());
    target->m_sampledImage = *sampledImage;
    const Core::DescriptorWriteItem view = Core::DescriptorWriteItem::TextureSrv(0u, target->m_color.get(), Core::Format::RGBA16_FLOAT, Core::s_AllSubresources, Core::TextureDimension::Texture2D);
    if(!heap.write(target->m_sampledImage, view))
        return MakeUnexpected(Failure{});
    return Move(target);
}

bool GpuRendererState::prepareBuffers(GpuFrameSlot& slot, const DrawSnapshot& snapshot){
    const usize vertexCount = snapshot.vertices().size();
    const usize indexCount = snapshot.indices().size();
    if(vertexCount == 0u && indexCount == 0u)
        return true;
    if(vertexCount == 0u || indexCount == 0u || vertexCount > Limit<usize>::s_Max / sizeof(Vertex) || indexCount > Limit<usize>::s_Max / sizeof(u32))
        return false;
    Core::Device& device = m_graphics.getDevice();
    if(slot.vertexCapacity < vertexCount){
        usize capacity = NextGrowingCapacity(slot.vertexCapacity, vertexCount, 4096u);
        if(capacity > Limit<usize>::s_Max / sizeof(Vertex))
            capacity = vertexCount;
        Core::BufferDesc description;
        description.setByteSize(static_cast<u64>(capacity * sizeof(Vertex))).setIsVertexBuffer(true)
            .setDebugName(Name("ui.vertices")).enableAutomaticStateTracking(Core::ResourceStates::Common)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
        ;
        auto bufferResult = device.createBuffer(description);
        if(!bufferResult)
            return false;
        Core::BufferHandle buffer = Move(*bufferResult);
        slot.vertices = Move(buffer);
        slot.vertexCapacity = capacity;
    }
    if(slot.indexCapacity < indexCount){
        usize capacity = NextGrowingCapacity(slot.indexCapacity, indexCount, 12288u);
        if(capacity > Limit<usize>::s_Max / sizeof(u32))
            capacity = indexCount;
        Core::BufferDesc description;
        description.setByteSize(static_cast<u64>(capacity * sizeof(u32))).setIsIndexBuffer(true)
            .setDebugName(Name("ui.indices")).enableAutomaticStateTracking(Core::ResourceStates::Common)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
        ;
        auto bufferResult = device.createBuffer(description);
        if(!bufferResult)
            return false;
        Core::BufferHandle buffer = Move(*bufferResult);
        slot.indices = Move(buffer);
        slot.indexCapacity = capacity;
    }
    return true;
}

bool GpuRendererState::prepareOutputPipeline(const Core::AcquiredPresentationFrame& acquired){
    const Core::FramebufferInfo& info = acquired.framebuffer->getFramebufferInfo();
    if(m_resources->m_outputPipeline && m_resources->m_outputPipeline->getFramebufferInfo() == info)
        return true;
    Core::Device& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    Core::RenderState renderState;
    renderState.depthStencilState.disableDepthTest().disableDepthWrite();
    renderState.rasterState.setCullNone();
    Core::GraphicsPipelineDesc description;
    description
        .setVertexShader(m_resources->m_outputVertexShader)
        .setPixelShader(m_resources->m_outputPixelShader)
        .setRenderState(renderState)
        .addBindingLayout(m_resources->m_outputLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    auto outputPipelineResult = device.createGraphicsPipeline(description, info);
    if(outputPipelineResult)
        m_resources->m_outputPipeline = Move(*outputPipelineResult);
    else
        m_resources->m_outputPipeline.reset();
    return static_cast<bool>(outputPipelineResult);
}

bool GpuRendererState::prepare(const Core::AcquiredPresentationFrame& acquired){
    m_readyToDeclare = false;
    if(!m_pending)
        return true;
    if(!acquired.valid() || !m_resources || m_width == 0u || m_height == 0u)
        return false;
    const Core::FramebufferDesc& attachments = acquired.framebuffer->getDescription();
    const Core::TextureDesc& backBuffer = acquired.backBuffer.texture->getDescription();
    if(
        attachments.colorAttachments.size() != 1u || attachments.colorAttachments[0].texture != acquired.backBuffer.texture.get()
        || attachments.depthAttachment.valid() || attachments.shadingRateAttachment.valid()
        || backBuffer.width != m_width || backBuffer.height != m_height || backBuffer.sampleCount != 1u
    )
        return false;
    if(m_pending->m_prepared && MatchesAcquired(m_pending->m_acquired, acquired)){
        m_readyToDeclare = true;
        return true;
    }
    Core::Device& device = m_graphics.getDevice();
    if(m_pending->m_prepared){
        // A failed graph may have accepted uploads or raster. Rebuild only after that exact prefix completes.
        if(!m_pending->prefixComplete(device))
            return true;
        m_pending->m_acquired = acquired;
        m_readyToDeclare = true;
        m_claimed = false;
        m_declaredGraph = nullptr;
        return true;
    }
    // Retire all completed owners before cache eviction or descriptor allocation can need their heap slots.
    for(auto& slot : m_slots){
        if(slot.inFlight && slot.inFlight->complete(device))
            slot.inFlight.reset();
    }
    trimImageCache(m_pending->m_snapshot);
    for(auto& slot : m_slots){
        if(slot.inFlight)
            continue;
        if(!prepareGlyphPages(*m_pending) || !prepareSdfPages(*m_pending) || !prepareTextureImages(*m_pending) || !prepareBuffers(slot, m_pending->m_snapshot) || !prepareOutputPipeline(acquired))
            return false;
        m_pending->m_target = slot.target;
        m_pending->m_resources = m_resources;
        m_pending->m_vertices = slot.vertices;
        m_pending->m_indices = slot.indices;
        m_pending->m_outputPipeline = m_resources->m_outputPipeline;
        m_pending->m_acquired = acquired;
        m_pending->m_presentationMode = m_graphics.isHDR10OutputActive() ? NWB_UI_PRESENTATION_HDR10 : NWB_UI_PRESENTATION_SDR;
        m_pending->m_prepared = true;
        m_readyToDeclare = true;
        slot.inFlight = m_pending;
        m_claimed = false;
        m_declaredGraph = nullptr;
        return true;
    }
    // Admission is bounded. The pending immutable CPU generation survives until a slot becomes available.
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

