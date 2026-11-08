// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend_resource_context.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GraphicsPipelineStateBuilder final{
public:
    static void AttachGraphicsPipelineFixedState(VkGraphicsPipelineCreateInfo& pipelineInfo, const VkPipelineRasterizationStateCreateInfo& rasterizer, const GraphicsPipelineFixedState& fixedState)noexcept;
    [[nodiscard]] static VkPipelineRasterizationStateCreateInfo BuildPipelineRasterizationState(const RasterState& rasterState, VkPolygonMode polygonMode, VkBool32 depthClampEnable)noexcept;
    [[nodiscard]] static Expected<GraphicsPipelineFixedState> BuildGraphicsPipelineFixedState(const FramebufferInfo& fbinfo, const RenderState& renderState, PipelineStencilFaceMode::Enum stencilFaceMode, Span<const VkDynamicState> dynamicStates, TStringView operationName, Alloc::ScratchArena& scratchArena);


private:
    [[nodiscard]] static VkPipelineColorBlendStateCreateInfo BuildPipelineColorBlendState(const FramebufferInfo& fbinfo, const BlendState& blendState, PipelineColorBlendAttachmentVector& outBlendAttachments);
    [[nodiscard]] static Expected<VkPipelineMultisampleStateCreateInfo> ConfigurePipelineMultisampleState(u32 sampleCount, b8 alphaToCoverageEnable, TStringView operationName);
    static void ConfigurePipelineDepthStencilState(const DepthStencilState& state, PipelineStencilFaceMode::Enum stencilFaceMode, VkPipelineDepthStencilStateCreateInfo& outState)noexcept;
    [[nodiscard]] static Expected<PipelineRenderingInfo> BuildPipelineRenderingInfo(const FramebufferInfo& fbinfo, TStringView operationName, Alloc::ScratchArena& scratchArena);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

