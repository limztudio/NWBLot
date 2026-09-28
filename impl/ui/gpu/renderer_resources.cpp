// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_internal.h"

#include <impl/assets/graphics/ui/names.h>
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

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commands,
        const Core::GpuTaskRecordContext& context){
        return context.declarations.textureForResource(payload.color) == payload.target->m_color.get() && !commands.commandRecordingFailed();
    }
};

[[nodiscard]] static Core::GpuTaskId DeclareTargetInitialization(void* const rawTarget, Core::GpuTaskGraph& graph){
    const auto& target = *static_cast<GpuVersion<GpuTargetVersion>*>(rawTarget);
    const Core::GpuGraphResourceId color = graph.importTexture(
        target->m_color,
        Core::GpuGraphResourceDesc().setIdentity(Name("ui.target_initialization")).setMarkerLabel("UI Target Initialization")
            .setType(Core::GpuGraphResourceType::Texture).setInitialState(Core::ResourceStates::Unknown)
            .setExternalFinalState(Core::ResourceStates::ShaderResource)
    );
    if(!color.valid())
        return {};
    const Core::GpuTaskId clear = graph.addClearTextureTask(
        Core::GpuTaskDesc().setIdentity(Name("ui.target_initialization_clear")).setMarkerLabel("UI Target Initialization Clear"),
        Core::GpuClearTextureTaskDesc{
            .destination = color,
            .valueType = Core::GpuClearTextureTaskValueType::Float,
            .floatValue = Core::Color(0.0f, 0.0f, 0.0f, 0.0f),
        }
    );
    if(!clear.valid())
        return {};
    const Core::GpuTaskResourceUse use{ color, {}, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read };
    return graph.addTask<TargetReadyTask>(
        Core::GpuTaskDesc().setIdentity(Name("ui.target_initialization_ready")).setMarkerLabel("UI Target Ready")
            .setDependencies(&clear, 1u).setResourceUses(&use, 1u),
        TargetReadyTask::Payload{ target, color }
    );
}

[[nodiscard]] static Core::RenderState PaintRenderState(){
    Core::RenderState state;
    state.depthStencilState.disableDepthTest().disableDepthWrite();
    state.rasterState.enableDepthClip().enableScissor().setCullNone();
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

[[nodiscard]] static bool LoadShader(
    GpuRendererState& state,
    Core::ShaderHandle& shader,
    const Name& identity,
    const Core::ShaderType::Mask stage){
    return ShaderAssetLoader::Load(
        shader, identity, Core::ShaderArchive::s_DefaultVariant, stage, identity,
        state.m_graphics, state.m_assets, state.m_resolver, MakeNotNull(NWB_TEXT("GpuRenderer"))
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuRendererState::matchesAcquired(
    const Core::AcquiredPresentationFrame& first,
    const Core::AcquiredPresentationFrame& second){
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
    GpuVersion<GpuSharedResources> resources = MakeGpuVersion<GpuSharedResources>(m_arena, m_graphics);
    if(!resources)
        return false;
    if(
        !__hidden_ui_gpu_resources::LoadShader(*this, resources->m_vertexShader, AssetsGraphicsUi::s_VertexShaderName, Core::ShaderType::Vertex)
        || !__hidden_ui_gpu_resources::LoadShader(*this, resources->m_pixelShader, AssetsGraphicsUi::s_PixelShaderName, Core::ShaderType::Pixel)
        || !__hidden_ui_gpu_resources::LoadShader(*this, resources->m_outputVertexShader, AssetsGraphicsUi::s_OutputVertexShaderName, Core::ShaderType::Vertex)
        || !__hidden_ui_gpu_resources::LoadShader(*this, resources->m_outputPixelShader, AssetsGraphicsUi::s_OutputPixelShaderName, Core::ShaderType::Pixel)
    )
        return false;
    Core::VertexAttributeDesc attributes[NWB_UI_VERTEX_ATTRIBUTE_COUNT];
    attributes[NWB_UI_VERTEX_POSITION_LOCATION]
        .setFormat(Core::Format::RG32_FLOAT).setBufferIndex(NWB_UI_VERTEX_BUFFER_INDEX)
        .setOffset(NWB_UI_VERTEX_POSITION_BYTE_OFFSET).setElementStride(sizeof(Vertex)).setName(Name("POSITION"))
    ;
    attributes[NWB_UI_VERTEX_UV_LOCATION]
        .setFormat(Core::Format::RG32_FLOAT).setBufferIndex(NWB_UI_VERTEX_BUFFER_INDEX)
        .setOffset(NWB_UI_VERTEX_UV_BYTE_OFFSET).setElementStride(sizeof(Vertex)).setName(Name("TEXCOORD"))
    ;
    attributes[NWB_UI_VERTEX_COLOR_LOCATION]
        .setFormat(Core::Format::RGBA32_FLOAT).setBufferIndex(NWB_UI_VERTEX_BUFFER_INDEX)
        .setOffset(NWB_UI_VERTEX_COLOR_BYTE_OFFSET).setElementStride(sizeof(Vertex)).setName(Name("COLOR"))
    ;
    resources->m_inputLayout = device.createInputLayout(attributes, NWB_UI_VERTEX_ATTRIBUTE_COUNT, resources->m_vertexShader.get());
    if(!resources->m_inputLayout)
        return false;
    Core::BindingLayoutDesc paintLayout(m_arena);
    paintLayout.setVisibility(Core::ShaderType::AllGraphics).addItem(Core::BindingLayoutItem::PushConstants(0u, sizeof(GpuPaintPushConstants)));
    resources->m_layout = device.createBindingLayout(paintLayout);
    Core::BindingLayoutDesc outputLayout(m_arena);
    outputLayout.setVisibility(Core::ShaderType::AllGraphics).addItem(Core::BindingLayoutItem::PushConstants(0u, sizeof(GpuOutputPushConstants)));
    resources->m_outputLayout = device.createBindingLayout(outputLayout);
    Core::SamplerDesc sampler;
    sampler.setAllFilters(true).setAllAddressModes(Core::SamplerAddressMode::Clamp);
    resources->m_sampler = device.createSampler(sampler);
    if(!resources->m_layout || !resources->m_outputLayout || !resources->m_sampler)
        return false;
    resources->m_samplerDescriptor = heap.allocate(Core::GpuDescriptorClass::Sampler);
    if(
        !resources->m_samplerDescriptor.valid()
        || !heap.write(resources->m_samplerDescriptor, Core::DescriptorWriteItem::Sampler(0u, resources->m_sampler.get()))
    )
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
    resources->m_pipeline = device.createGraphicsPipeline(pipeline, Core::FramebufferInfo().addColorFormat(Core::Format::RGBA16_FLOAT));
    if(!resources->m_pipeline)
        return false;
    m_resources = Move(resources);
    return true;
}

GpuVersion<GpuTargetVersion> GpuRendererState::createTarget(const u32 width, const u32 height){
    GpuVersion<GpuTargetVersion> target = MakeGpuVersion<GpuTargetVersion>(m_arena, m_graphics);
    if(!target)
        return {};
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
    target->m_color = device.createTexture(description);
    if(!target->m_color)
        return {};
    target->m_framebuffer = device.createFramebuffer(Core::FramebufferDesc().addColorAttachment(target->m_color.get()));
    if(!target->m_framebuffer)
        return {};
    const Core::GpuPhysicalQueueId queue = device.getPrimaryPhysicalQueue(Core::CommandQueue::Graphics);
    // A creation descriptor does not transition a fresh native image. Initialize before publishing its bindless view.
    if(!queue.valid())
        return {};
    if(!m_graphics.submitStandaloneTaskGraph(
        &target, __hidden_ui_gpu_resources::DeclareTargetInitialization, target->m_readinessToken, queue
    ))
        return {};
    if(!target->m_readinessToken.valid() || !target->m_readinessToken.hasPhysicalQueueIdentity())
        return {};
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    target->m_sampledImage = heap.allocate(Core::GpuDescriptorClass::SampledImage);
    const Core::DescriptorWriteItem view = Core::DescriptorWriteItem::Texture_SRV(
        0u, target->m_color.get(), Core::Format::RGBA16_FLOAT, Core::s_AllSubresources, Core::TextureDimension::Texture2D
    );
    if(!target->m_sampledImage.valid() || !heap.write(target->m_sampledImage, view))
        return {};
    return target;
}

bool GpuRendererState::prepareBuffers(GpuFrameSlot& slot, const DrawSnapshot& snapshot){
    const usize vertexCount = snapshot.vertices().size();
    const usize indexCount = snapshot.indices().size();
    if(vertexCount == 0u && indexCount == 0u)
        return true;
    if(
        vertexCount == 0u || indexCount == 0u || vertexCount > Limit<usize>::s_Max / sizeof(Vertex)
        || indexCount > Limit<usize>::s_Max / sizeof(u32)
    )
        return false;
    Core::Device& device = m_graphics.getDevice();
    if(slot.vertexCapacity < vertexCount){
        const usize capacity = Max(vertexCount, static_cast<usize>(4096u));
        Core::BufferDesc description;
        description.setByteSize(static_cast<u64>(capacity * sizeof(Vertex))).setIsVertexBuffer(true)
            .setDebugName(Name("ui.vertices")).enableAutomaticStateTracking(Core::ResourceStates::Common)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
        ;
        Core::BufferHandle buffer = device.createBuffer(description);
        if(!buffer)
            return false;
        slot.vertices = Move(buffer);
        slot.vertexCapacity = capacity;
    }
    if(slot.indexCapacity < indexCount){
        const usize capacity = Max(indexCount, static_cast<usize>(12288u));
        Core::BufferDesc description;
        description.setByteSize(static_cast<u64>(capacity * sizeof(u32))).setIsIndexBuffer(true)
            .setDebugName(Name("ui.indices")).enableAutomaticStateTracking(Core::ResourceStates::Common)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
        ;
        Core::BufferHandle buffer = device.createBuffer(description);
        if(!buffer)
            return false;
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
    m_resources->m_outputPipeline = device.createGraphicsPipeline(description, info);
    return m_resources->m_outputPipeline != nullptr;
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
    if(m_pending->m_prepared && matchesAcquired(m_pending->m_acquired, acquired)){
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
    for(auto& slot : m_slots){
        if(slot.inFlight && !slot.inFlight->complete(device))
            continue;
        if(!prepareGlyphPages(*m_pending) || !prepareBuffers(slot, m_pending->m_snapshot) || !prepareOutputPipeline(acquired))
            return false;
        slot.inFlight.reset();
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

