// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN

using __hidden_vulkan::checked_cast;
using namespace __hidden_vulkan;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// GraphicsPipeline - Graphics pipeline state object
//-----------------------------------------------------------------------------

GraphicsPipeline::~GraphicsPipeline(){
    if(pipeline){
        vkDestroyPipeline(m_context.device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
}

Object GraphicsPipeline::getNativeHandle(ObjectType objectType){
    if(objectType == ObjectTypes::VK_Pipeline)
        return Object(pipeline);
    return Object(nullptr);
}

//-----------------------------------------------------------------------------
// Device - Framebuffer creation
//-----------------------------------------------------------------------------

FramebufferHandle Device::createFramebuffer(const FramebufferDesc& desc){
    Framebuffer* fb = new Framebuffer(m_context);
    fb->desc = desc;
    
    // Calculate framebuffer info from attachments
    
    // Store color attachments
    for(u32 i = 0; i < static_cast<u32>(desc.colorAttachments.size()); i++){
        if(desc.colorAttachments[i].texture){
            fb->resources.push_back(desc.colorAttachments[i].texture);
            Texture* tex = checked_cast<Texture*>(desc.colorAttachments[i].texture);
            fb->framebufferInfo.colorFormats.push_back(tex->desc.format);
            
            if(fb->framebufferInfo.width == 0)
                fb->framebufferInfo.width = tex->desc.width;
            if(fb->framebufferInfo.height == 0)
                fb->framebufferInfo.height = tex->desc.height;
        }
    }
    
    // Store depth attachment
    if(desc.depthAttachment.texture){
        fb->resources.push_back(desc.depthAttachment.texture);
        Texture* depthTex = checked_cast<Texture*>(desc.depthAttachment.texture);
        fb->framebufferInfo.depthFormat = depthTex->desc.format;
        
        if(fb->framebufferInfo.width == 0)
            fb->framebufferInfo.width = depthTex->desc.width;
        if(fb->framebufferInfo.height == 0)
            fb->framebufferInfo.height = depthTex->desc.height;
    }
    
    // Get sample count from first valid attachment
    if(!fb->resources.empty()){
        Texture* tex = checked_cast<Texture*>(fb->resources[0].get());
        fb->framebufferInfo.sampleCount = tex->desc.sampleCount;
    }
    
    return FramebufferHandle(fb, AdoptRef);
}

//-----------------------------------------------------------------------------
// Device - Graphics pipeline creation
//-----------------------------------------------------------------------------

GraphicsPipelineHandle Device::createGraphicsPipeline(const GraphicsPipelineDesc& desc, IFramebuffer* _fb){
    // TODO: Implement graphics pipeline creation
    // This is a complex operation involving:
    // 1. Shader stages setup
    // 2. Vertex input state
    // 3. Input assembly state
    // 4. Viewport/scissor state (dynamic)
    // 5. Rasterization state
    // 6. Multisample state
    // 7. Depth/stencil state
    // 8. Color blend state
    // 9. Dynamic state
    // 10. Pipeline layout
    // 11. Render pass or dynamic rendering
    
    GraphicsPipeline* pso = new GraphicsPipeline(m_context);
    pso->desc = desc;
    
    // Step 1: Collect shader stages
    Vector<VkPipelineShaderStageCreateInfo> shaderStages;
    shaderStages.reserve(5);
    
    if(desc.VS){
        Shader* vs = checked_cast<Shader*>(desc.VS.get());
        VkPipelineShaderStageCreateInfo stageInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        stageInfo.module = vs->shaderModule;
        stageInfo.pName = vs->desc.entryName.c_str();
        shaderStages.push_back(stageInfo);
    }
    
    if(desc.HS){
        Shader* hs = checked_cast<Shader*>(desc.HS.get());
        VkPipelineShaderStageCreateInfo stageInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stageInfo.stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        stageInfo.module = hs->shaderModule;
        stageInfo.pName = hs->desc.entryName.c_str();
        shaderStages.push_back(stageInfo);
    }
    
    if(desc.DS){
        Shader* ds = checked_cast<Shader*>(desc.DS.get());
        VkPipelineShaderStageCreateInfo stageInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stageInfo.stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        stageInfo.module = ds->shaderModule;
        stageInfo.pName = ds->desc.entryName.c_str();
        shaderStages.push_back(stageInfo);
    }
    
    if(desc.GS){
        Shader* gs = checked_cast<Shader*>(desc.GS.get());
        VkPipelineShaderStageCreateInfo stageInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stageInfo.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
        stageInfo.module = gs->shaderModule;
        stageInfo.pName = gs->desc.entryName.c_str();
        shaderStages.push_back(stageInfo);
    }
    
    if(desc.PS){
        Shader* ps = checked_cast<Shader*>(desc.PS.get());
        VkPipelineShaderStageCreateInfo stageInfo = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stageInfo.module = ps->shaderModule;
        stageInfo.pName = ps->desc.entryName.c_str();
        shaderStages.push_back(stageInfo);
    }
    
    // Step 2: Vertex input state from InputLayout
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    Vector<VkVertexInputBindingDescription> bindings;
    Vector<VkVertexInputAttributeDescription> attributes;
    
    if(desc.inputLayout){
        InputLayout* layout = checked_cast<InputLayout*>(desc.inputLayout.get());
        bindings = layout->bindings;
        attributes = layout->vkAttributes;
    }
    
    vertexInputInfo.vertexBindingDescriptionCount = (u32)bindings.size();
    vertexInputInfo.pVertexBindingDescriptions = bindings.data();
    vertexInputInfo.vertexAttributeDescriptionCount = (u32)attributes.size();
    vertexInputInfo.pVertexAttributeDescriptions = attributes.data();
    
    // Step 3: Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; // TODO: Convert from desc.primType
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    
    // Step 4: Viewport and scissor (dynamic)
    VkPipelineViewportStateCreateInfo viewportState = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    
    // Step 5: Rasterization state
    VkPipelineRasterizationStateCreateInfo rasterizer = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL; // TODO: Convert from desc.renderState.fillMode
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;   // TODO: Convert from desc.renderState.cullMode
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = desc.renderState.rasterState.depthBias != 0 ? VK_TRUE : VK_FALSE;
    rasterizer.depthBiasConstantFactor = (f32)desc.renderState.rasterState.depthBias;
    rasterizer.depthBiasClamp = desc.renderState.rasterState.depthBiasClamp;
    rasterizer.depthBiasSlopeFactor = desc.renderState.rasterState.slopeScaledDepthBias;
    rasterizer.lineWidth = 1.0f;
    
    // Step 6: Multisample state
    VkPipelineMultisampleStateCreateInfo multisampling = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT; // TODO: Get from framebuffer
    multisampling.sampleShadingEnable = VK_FALSE;
    
    // Step 7: Depth/stencil state
    VkPipelineDepthStencilStateCreateInfo depthStencil = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depthStencil.depthTestEnable = desc.renderState.depthStencilState.depthTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = desc.renderState.depthStencilState.depthWriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS; // TODO: Convert from desc.renderState.depthFunc
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = desc.renderState.depthStencilState.stencilEnable ? VK_TRUE : VK_FALSE;
    // TODO: Fill front and back stencil ops
    
    // Step 8: Color blend state
    VkPipelineColorBlendStateCreateInfo colorBlending = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    Vector<VkPipelineColorBlendAttachmentState> blendAttachments;
    // TODO: Create blend attachment for each render target
    colorBlending.attachmentCount = (u32)blendAttachments.size();
    colorBlending.pAttachments = blendAttachments.data();
    
    // Step 9: Dynamic state
    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_LINE_WIDTH,
        VK_DYNAMIC_STATE_DEPTH_BIAS,
        VK_DYNAMIC_STATE_BLEND_CONSTANTS,
        VK_DYNAMIC_STATE_DEPTH_BOUNDS,
        VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE,
    };
    
    VkPipelineDynamicStateCreateInfo dynamicState = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamicState.dynamicStateCount = sizeof(dynamicStates) / sizeof(dynamicStates[0]);
    dynamicState.pDynamicStates = dynamicStates;
    
    // Step 10: Pipeline layout
    // TODO: Get from binding layout
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    
    // Step 11: Create pipeline with dynamic rendering
    VkPipelineRenderingCreateInfo renderingInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    // TODO: Fill color/depth formats from framebuffer
    
    VkGraphicsPipelineCreateInfo pipelineInfo = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = (u32)shaderStages.size();
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = VK_NULL_HANDLE; // Using dynamic rendering
    pipelineInfo.subpass = 0;
    
    VkResult res = vkCreateGraphicsPipelines(m_context.device, m_context.pipelineCache, 1, &pipelineInfo, m_context.allocationCallbacks, &pso->pipeline);
    
    if(res != VK_SUCCESS){
        delete pso;
        return nullptr;
    }
    
    return GraphicsPipelineHandle(pso, AdoptRef);
}

//-----------------------------------------------------------------------------
// CommandList - Graphics
//-----------------------------------------------------------------------------

struct RenderPassParameters{
    bool clearColorTargets = false;
    Color colorClearValues[8]{};
    u8 colorClearMask = 0xff;
    bool clearDepthTarget = false;
    f32 depthClearValue = 1.0f;
    bool clearStencilTarget = false;
    u8 stencilClearValue = 0;
    
    [[nodiscard]] bool clearColorTarget(u32 index)const{ return (colorClearMask & (1u << index)) != 0; }
};

void CommandList::beginRenderPass(IFramebuffer* _framebuffer, const RenderPassParameters& params){
    Framebuffer* fb = checked_cast<Framebuffer*>(_framebuffer);
    const FramebufferDesc& fbDesc = fb->desc;
    
    // Dynamic rendering (VK_KHR_dynamic_rendering)
    VkRenderingAttachmentInfo colorAttachments[8] = {};
    u32 numColorAttachments = 0;
    
    for(u32 i = 0; i < static_cast<u32>(fbDesc.colorAttachments.size()); i++){
        if(fbDesc.colorAttachments[i].texture){
            Texture* tex = checked_cast<Texture*>(fbDesc.colorAttachments[i].texture);
            
            VkImageView view = tex->getView(fbDesc.colorAttachments[i].subresources, TextureDimension::Texture2D, Format::UNKNOWN);
            
            colorAttachments[numColorAttachments].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colorAttachments[numColorAttachments].imageView = view;
            colorAttachments[numColorAttachments].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            
            if(params.clearColorTargets && params.clearColorTarget(i)){
                colorAttachments[numColorAttachments].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                const Color& clr = params.colorClearValues[i];
                colorAttachments[numColorAttachments].clearValue.color = { clr.r, clr.g, clr.b, clr.a };
            }
            else
                colorAttachments[numColorAttachments].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            
            colorAttachments[numColorAttachments].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            numColorAttachments++;
        }
    }
    
    VkRenderingAttachmentInfo depthAttachment = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    VkRenderingAttachmentInfo stencilAttachment = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    bool hasDepth = false;
    bool hasStencil = false;
    
    if(fbDesc.depthAttachment.texture){
        Texture* depthTex = checked_cast<Texture*>(fbDesc.depthAttachment.texture);
        VkImageView depthView = depthTex->getView(fbDesc.depthAttachment.subresources, TextureDimension::Texture2D, Format::UNKNOWN, true);
        
        const FormatInfo& formatInfo = GetFormatInfo(depthTex->desc.format);
        if(formatInfo.hasDepth){
            depthAttachment.imageView = depthView;
            depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthAttachment.loadOp = params.clearDepthTarget ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthAttachment.clearValue.depthStencil.depth = params.depthClearValue;
            hasDepth = true;
        }
        if(formatInfo.hasStencil){
            stencilAttachment.imageView = depthView;
            stencilAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            stencilAttachment.loadOp = params.clearStencilTarget ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
            stencilAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            stencilAttachment.clearValue.depthStencil.stencil = params.stencilClearValue;
            hasStencil = true;
        }
    }
    
    VkRenderingInfo renderingInfo = { VK_STRUCTURE_TYPE_RENDERING_INFO };
    renderingInfo.renderArea.offset = { 0, 0 };
    renderingInfo.renderArea.extent = { fb->framebufferInfo.width, fb->framebufferInfo.height };
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = numColorAttachments;
    renderingInfo.pColorAttachments = colorAttachments;
    if(hasDepth)
        renderingInfo.pDepthAttachment = &depthAttachment;
    if(hasStencil)
        renderingInfo.pStencilAttachment = &stencilAttachment;
    
    vkCmdBeginRendering(currentCmdBuf->cmdBuf, &renderingInfo);
}

void CommandList::endRenderPass(){
    vkCmdEndRendering(currentCmdBuf->cmdBuf);
}

void CommandList::setGraphicsState(const GraphicsState& state){
    currentGraphicsState = state;
    
    GraphicsPipeline* pipeline = checked_cast<GraphicsPipeline*>(state.pipeline);
    
    if(pipeline)
        vkCmdBindPipeline(currentCmdBuf->cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
    
    // Bind descriptor sets
    if(state.bindings.size() > 0){
        for(usize i = 0; i < state.bindings.size(); i++){
            if(state.bindings[i]){
                BindingSet* bindingSet = checked_cast<BindingSet*>(state.bindings[i]);
                if(!bindingSet->descriptorSets.empty())
                    vkCmdBindDescriptorSets(currentCmdBuf->cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        pipeline->pipelineLayout, static_cast<u32>(i),
                        static_cast<u32>(bindingSet->descriptorSets.size()),
                        bindingSet->descriptorSets.data(), 0, nullptr);
            }
        }
    }
    
    // Set viewport and scissor
    if(!state.viewport.viewports.empty()){
        const auto& vp = state.viewport.viewports[0];
        VkViewport viewport{};
        viewport.x = vp.minX;
        viewport.y = vp.minY;
        viewport.width = vp.maxX - vp.minX;
        viewport.height = vp.maxY - vp.minY;
        viewport.minDepth = vp.minZ;
        viewport.maxDepth = vp.maxZ;
        vkCmdSetViewport(currentCmdBuf->cmdBuf, 0, 1, &viewport);
        
        VkRect2D scissor{};
        if(!state.viewport.scissorRects.empty()){
            const auto& sr = state.viewport.scissorRects[0];
            scissor.offset = { sr.minX, sr.minY };
            scissor.extent = { static_cast<u32>(sr.maxX - sr.minX), static_cast<u32>(sr.maxY - sr.minY) };
        }
        else{
            scissor.offset = { (i32)vp.minX, (i32)vp.minY };
            scissor.extent = { (u32)(vp.maxX - vp.minX), (u32)(vp.maxY - vp.minY) };
        }
        vkCmdSetScissor(currentCmdBuf->cmdBuf, 0, 1, &scissor);
    }
    
    // Bind vertex buffers
    if(!state.vertexBuffers.empty()){
        VkBuffer vertexBuffers[16];
        VkDeviceSize offsets[16];
        u32 count = static_cast<u32>(state.vertexBuffers.size());
        for(u32 i = 0; i < count; i++){
            Buffer* vb = checked_cast<Buffer*>(state.vertexBuffers[i].buffer);
            vertexBuffers[i] = vb->buffer;
            offsets[i] = state.vertexBuffers[i].offset;
        }
        vkCmdBindVertexBuffers(currentCmdBuf->cmdBuf, 0, count, vertexBuffers, offsets);
    }
    
    // Bind index buffer
    if(state.indexBuffer.buffer){
        Buffer* ib = checked_cast<Buffer*>(state.indexBuffer.buffer);
        VkIndexType indexType = (state.indexBuffer.format == Format::R16_UINT) ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
        vkCmdBindIndexBuffer(currentCmdBuf->cmdBuf, ib->buffer, state.indexBuffer.offset, indexType);
    }
}

void CommandList::draw(const DrawArguments& args){
    vkCmdDraw(currentCmdBuf->cmdBuf, args.vertexCount, args.instanceCount, args.startVertexLocation, args.startInstanceLocation);
}

void CommandList::drawIndexed(const DrawArguments& args){
    vkCmdDrawIndexed(currentCmdBuf->cmdBuf, args.vertexCount, args.instanceCount, args.startIndexLocation, args.startVertexLocation, args.startInstanceLocation);
}

void CommandList::drawIndirect(u32 offsetBytes, u32 drawCount){
    if(!currentGraphicsState.indirectParams){
        NWB_ASSERT(false && "No indirect buffer bound for drawIndirect");
        return;
    }
    Buffer* indirectBuffer = checked_cast<Buffer*>(currentGraphicsState.indirectParams);
    vkCmdDrawIndirect(currentCmdBuf->cmdBuf, indirectBuffer->buffer, offsetBytes, drawCount, sizeof(DrawIndirectArguments));
    currentCmdBuf->referencedResources.push_back(currentGraphicsState.indirectParams);
}

namespace __hidden_vulkan{

//-----------------------------------------------------------------------------
// Helper functions for state conversion
//-----------------------------------------------------------------------------

VkPrimitiveTopology convertPrimitiveTopology(PrimitiveType::Enum primType){
    switch(primType){
    case PrimitiveType::PointList:     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case PrimitiveType::LineList:      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case PrimitiveType::TriangleList:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case PrimitiveType::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case PrimitiveType::TriangleFan:   return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    case PrimitiveType::TriangleListWithAdjacency: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY;
    case PrimitiveType::TriangleStripWithAdjacency: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY;
    case PrimitiveType::PatchList:     return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

VkCullModeFlags convertCullMode(RasterCullMode::Enum cullMode){
    switch(cullMode){
    case RasterCullMode::Back:  return VK_CULL_MODE_BACK_BIT;
    case RasterCullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    case RasterCullMode::None:  return VK_CULL_MODE_NONE;
    default: return VK_CULL_MODE_BACK_BIT;
    }
}

VkPolygonMode convertFillMode(RasterFillMode::Enum fillMode){
    switch(fillMode){
    case RasterFillMode::Solid:     return VK_POLYGON_MODE_FILL;
    case RasterFillMode::Wireframe: return VK_POLYGON_MODE_LINE;
    default: return VK_POLYGON_MODE_FILL;
    }
}

VkCompareOp convertCompareOp(ComparisonFunc::Enum compareFunc){
    switch(compareFunc){
    case ComparisonFunc::Never:        return VK_COMPARE_OP_NEVER;
    case ComparisonFunc::Less:         return VK_COMPARE_OP_LESS;
    case ComparisonFunc::Equal:        return VK_COMPARE_OP_EQUAL;
    case ComparisonFunc::LessOrEqual:  return VK_COMPARE_OP_LESS_OR_EQUAL;
    case ComparisonFunc::Greater:      return VK_COMPARE_OP_GREATER;
    case ComparisonFunc::NotEqual:     return VK_COMPARE_OP_NOT_EQUAL;
    case ComparisonFunc::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case ComparisonFunc::Always:       return VK_COMPARE_OP_ALWAYS;
    default: return VK_COMPARE_OP_ALWAYS;
    }
}

VkStencilOp convertStencilOp(StencilOp::Enum stencilOp){
    switch(stencilOp){
    case StencilOp::Keep:              return VK_STENCIL_OP_KEEP;
    case StencilOp::Zero:              return VK_STENCIL_OP_ZERO;
    case StencilOp::Replace:           return VK_STENCIL_OP_REPLACE;
    case StencilOp::IncrementAndClamp: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case StencilOp::DecrementAndClamp: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    case StencilOp::Invert:            return VK_STENCIL_OP_INVERT;
    case StencilOp::IncrementAndWrap:  return VK_STENCIL_OP_INCREMENT_AND_WRAP;
    case StencilOp::DecrementAndWrap:  return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    default: return VK_STENCIL_OP_KEEP;
    }
}

VkBlendFactor convertBlendFactor(BlendFactor::Enum blendFactor){
    switch(blendFactor){
    case BlendFactor::Zero:             return VK_BLEND_FACTOR_ZERO;
    case BlendFactor::One:              return VK_BLEND_FACTOR_ONE;
    case BlendFactor::SrcColor:         return VK_BLEND_FACTOR_SRC_COLOR;
    case BlendFactor::InvSrcColor:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case BlendFactor::SrcAlpha:         return VK_BLEND_FACTOR_SRC_ALPHA;
    case BlendFactor::InvSrcAlpha:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DstAlpha:         return VK_BLEND_FACTOR_DST_ALPHA;
    case BlendFactor::InvDstAlpha:      return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case BlendFactor::DstColor:         return VK_BLEND_FACTOR_DST_COLOR;
    case BlendFactor::InvDstColor:      return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case BlendFactor::SrcAlphaSaturate: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    case BlendFactor::ConstantColor:    return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case BlendFactor::InvConstantColor: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case BlendFactor::Src1Color:        return VK_BLEND_FACTOR_SRC1_COLOR;
    case BlendFactor::InvSrc1Color:     return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
    case BlendFactor::Src1Alpha:        return VK_BLEND_FACTOR_SRC1_ALPHA;
    case BlendFactor::InvSrc1Alpha:     return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
    default: return VK_BLEND_FACTOR_ZERO;
    }
}

VkBlendOp convertBlendOp(BlendOp::Enum blendOp){
    switch(blendOp){
    case BlendOp::Add:             return VK_BLEND_OP_ADD;
    case BlendOp::Subtract:        return VK_BLEND_OP_SUBTRACT;
    case BlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
    case BlendOp::Min:             return VK_BLEND_OP_MIN;
    case BlendOp::Max:             return VK_BLEND_OP_MAX;
    default: return VK_BLEND_OP_ADD;
    }
}

VkStencilOpState convertStencilOpState(const DepthStencilState& dsState, const DepthStencilState::StencilOpDesc& stencilDesc){
    VkStencilOpState state = {};
    state.failOp = convertStencilOp(stencilDesc.failOp);
    state.passOp = convertStencilOp(stencilDesc.passOp);
    state.depthFailOp = convertStencilOp(stencilDesc.depthFailOp);
    state.compareOp = convertCompareOp(stencilDesc.stencilFunc);
    state.compareMask = dsState.stencilReadMask;
    state.writeMask = dsState.stencilWriteMask;
    state.reference = dsState.stencilRefValue;
    return state;
}

VkPipelineColorBlendAttachmentState convertBlendState(const BlendState::RenderTarget& target){
    VkPipelineColorBlendAttachmentState state = {};
    state.blendEnable = target.blendEnable ? VK_TRUE : VK_FALSE;
    state.srcColorBlendFactor = convertBlendFactor(target.srcBlend);
    state.dstColorBlendFactor = convertBlendFactor(target.destBlend);
    state.colorBlendOp = convertBlendOp(target.blendOp);
    state.srcAlphaBlendFactor = convertBlendFactor(target.srcBlendAlpha);
    state.dstAlphaBlendFactor = convertBlendFactor(target.destBlendAlpha);
    state.alphaBlendOp = convertBlendOp(target.blendOpAlpha);
    state.colorWriteMask = 0;
    if(target.colorWriteMask & ColorMask::Red)   state.colorWriteMask |= VK_COLOR_COMPONENT_R_BIT;
    if(target.colorWriteMask & ColorMask::Green) state.colorWriteMask |= VK_COLOR_COMPONENT_G_BIT;
    if(target.colorWriteMask & ColorMask::Blue)  state.colorWriteMask |= VK_COLOR_COMPONENT_B_BIT;
    if(target.colorWriteMask & ColorMask::Alpha) state.colorWriteMask |= VK_COLOR_COMPONENT_A_BIT;
    return state;
}

} // namespace __hidden_vulkan


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
