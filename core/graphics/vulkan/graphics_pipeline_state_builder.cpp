// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graphics_pipeline_state_builder.h"
#include "backend.h"
#include "command_validation.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GraphicsPipelineStateBuilder::AttachGraphicsPipelineFixedState(VkGraphicsPipelineCreateInfo& pipelineInfo, const VkPipelineRasterizationStateCreateInfo& rasterizer, const GraphicsPipelineFixedState& fixedState)noexcept{
    pipelineInfo.pViewportState = &fixedState.viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &fixedState.multisampling;
    pipelineInfo.pDepthStencilState = &fixedState.depthStencil;
    pipelineInfo.pColorBlendState = &fixedState.colorBlending;
    pipelineInfo.pDynamicState = &fixedState.dynamicState;
}

VkPipelineRasterizationStateCreateInfo GraphicsPipelineStateBuilder::BuildPipelineRasterizationState(const RasterState& rasterState, const VkPolygonMode polygonMode, const VkBool32 depthClampEnable)noexcept{
    auto rasterizer = MakeVkStruct<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    rasterizer.depthClampEnable = depthClampEnable;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = polygonMode;
    rasterizer.cullMode = ConvertCullMode(rasterState.cullMode);
    rasterizer.frontFace = rasterState.frontCounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = rasterState.depthBias != 0 || rasterState.slopeScaledDepthBias != 0.0f ? VK_TRUE : VK_FALSE;
    rasterizer.depthBiasConstantFactor = static_cast<f32>(rasterState.depthBias);
    rasterizer.depthBiasClamp = rasterState.depthBiasClamp;
    rasterizer.depthBiasSlopeFactor = rasterState.slopeScaledDepthBias;
    rasterizer.lineWidth = s_DefaultRasterLineWidth;
    return rasterizer;
}

VkPipelineColorBlendStateCreateInfo GraphicsPipelineStateBuilder::BuildPipelineColorBlendState(const FramebufferInfo& fbinfo, const BlendState& blendState, PipelineColorBlendAttachmentVector& outBlendAttachments){
    const usize colorFormatCount = fbinfo.colorFormats.size();
    outBlendAttachments.clear();
    outBlendAttachments.reserve(colorFormatCount);
    for(usize i = 0u; i < colorFormatCount; ++i)
        outBlendAttachments.push_back(ConvertBlendState(blendState.targets[i]));

    auto colorBlending = MakeVkStruct<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = static_cast<u32>(outBlendAttachments.size());
    colorBlending.pAttachments = outBlendAttachments.data();
    return colorBlending;
}

Expected<VkPipelineMultisampleStateCreateInfo> GraphicsPipelineStateBuilder::ConfigurePipelineMultisampleState(const u32 sampleCount, const b8 alphaToCoverageEnable, TStringView operationName){
    auto state = MakeVkStruct<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    if(!IsSupportedSampleCount(sampleCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: sample count {} is unsupported"), operationName, sampleCount);
        return MakeUnexpected(Failure{});
    }
    state.rasterizationSamples = GetSampleCountFlagBits(sampleCount);
    state.sampleShadingEnable = VK_FALSE;
    state.alphaToCoverageEnable = alphaToCoverageEnable ? VK_TRUE : VK_FALSE;
    return state;
}

void GraphicsPipelineStateBuilder::ConfigurePipelineDepthStencilState(const DepthStencilState& state, PipelineStencilFaceMode::Enum stencilFaceMode, VkPipelineDepthStencilStateCreateInfo& outState)noexcept{
    outState = MakeVkStruct<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
    outState.depthTestEnable = state.depthTestEnable ? VK_TRUE : VK_FALSE;
    outState.depthWriteEnable = state.depthWriteEnable ? VK_TRUE : VK_FALSE;
    outState.depthCompareOp = ConvertCompareOp(state.depthFunc);
    outState.depthBoundsTestEnable = VK_FALSE;
    outState.stencilTestEnable = state.stencilEnable ? VK_TRUE : VK_FALSE;
    if(stencilFaceMode == PipelineStencilFaceMode::IncludeStencilFaces){
        outState.front = ConvertStencilOpState(state, state.frontFaceStencil);
        outState.back = ConvertStencilOpState(state, state.backFaceStencil);
    }
}

Expected<GraphicsPipelineFixedState> GraphicsPipelineStateBuilder::BuildGraphicsPipelineFixedState(
    const FramebufferInfo& fbinfo,
    const RenderState& renderState,
    const PipelineStencilFaceMode::Enum stencilFaceMode,
    const Span<const VkDynamicState> dynamicStates,
    TStringView operationName,
    Alloc::ScratchArena& scratchArena
){
    GraphicsPipelineFixedState state(scratchArena);
    state.viewportState = MakeVkStruct<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    state.viewportState.viewportCount = 1;
    state.viewportState.scissorCount = 1;

    const auto multisampling = ConfigurePipelineMultisampleState(fbinfo.sampleCount, renderState.blendState.alphaToCoverageEnable, operationName);
    if(!multisampling)
        return MakeUnexpected(Failure{});
    state.multisampling = *multisampling;

    ConfigurePipelineDepthStencilState(renderState.depthStencilState, stencilFaceMode, state.depthStencil);

    state.colorBlending = BuildPipelineColorBlendState(fbinfo, renderState.blendState, state.blendAttachments);

    state.dynamicState = MakeVkStruct<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
    state.dynamicState.dynamicStateCount = static_cast<u32>(dynamicStates.size());
    state.dynamicState.pDynamicStates = dynamicStates.data();

    auto rendering = BuildPipelineRenderingInfo(fbinfo, operationName, scratchArena);
    if(!rendering)
        return MakeUnexpected(Failure{});
    state.renderingInfo = rendering->info;
    state.colorFormats = Move(rendering->colorFormats);
    return state;
}

Expected<PipelineRenderingInfo> GraphicsPipelineStateBuilder::BuildPipelineRenderingInfo(const FramebufferInfo& fbinfo, TStringView operationName, Alloc::ScratchArena& scratchArena){
    PipelineRenderingInfo rendering(scratchArena);
    rendering.colorFormats.reserve(fbinfo.colorFormats.size());
    for(u32 i = 0u; i < static_cast<u32>(fbinfo.colorFormats.size()); ++i){
        const VkFormat vkFormat = ConvertFormat(fbinfo.colorFormats[i]);
        if(vkFormat == VK_FORMAT_UNDEFINED){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: color attachment format {} is unsupported"), operationName, i);
            return MakeUnexpected(Failure{});
        }
        if(!VulkanDetail::IsPipelineColorAttachmentFormatClassValid(fbinfo.colorFormats[i])){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: color attachment format {} has depth or stencil aspects"), operationName, i);
            return MakeUnexpected(Failure{});
        }
        rendering.colorFormats.push_back(vkFormat);
    }

    rendering.info = MakeVkStruct<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
    rendering.info.colorAttachmentCount = static_cast<u32>(rendering.colorFormats.size());
    rendering.info.pColorAttachmentFormats = rendering.colorFormats.data();
    if(fbinfo.depthFormat != Format::UNKNOWN){
        const VkFormat vkDepthFormat = ConvertFormat(fbinfo.depthFormat);
        if(vkDepthFormat == VK_FORMAT_UNDEFINED){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: depth/stencil attachment format is unsupported"), operationName);
            return MakeUnexpected(Failure{});
        }
        const FormatInfo& depthFormatInfo = GetFormatInfo(fbinfo.depthFormat);
        if(!depthFormatInfo.hasDepth && !depthFormatInfo.hasStencil){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: depth/stencil attachment format has no depth or stencil aspect"), operationName);
            return MakeUnexpected(Failure{});
        }
        if(depthFormatInfo.hasDepth)
            rendering.info.depthAttachmentFormat = vkDepthFormat;
        if(depthFormatInfo.hasStencil)
            rendering.info.stencilAttachmentFormat = vkDepthFormat;
    }

    return rendering;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

