// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/graphics/backend_selection/raster_types.h>

#include "backend.h"
#include "command_validation.h"
#include "vulkan_viewport_policy.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline VertexInputBinding BuildVertexInputBinding(const VkVertexInputBindingDescription* binding)noexcept{
    return binding ? VertexInputBinding{ .slot = binding->binding, .stride = binding->stride, .instanceRate = binding->inputRate == VK_VERTEX_INPUT_RATE_INSTANCE, .valid = true } : VertexInputBinding{};
}

[[nodiscard]] inline VertexInputBinding GetVertexInputBinding(const InputLayout& layout, const u32 index)noexcept{ return BuildVertexInputBinding(layout.getBindingDescription(index)); }

[[nodiscard]] inline b8 IsViewportValid(const Viewport& viewport, const Device& device)noexcept{ return VulkanViewportPolicy::isViewportValid(viewport, device.getPhysicalDeviceLimits()); }

[[nodiscard]] inline b8 IsImplicitScissorValid(const Viewport& viewport)noexcept{ return VulkanViewportPolicy::isImplicitScissorValid(viewport); }
using VulkanDetail::IsFramebufferAttachmentSubresourceSetValid;
using VulkanDetail::GetIndexElementByteSize;
using VulkanDetail::IsIndexDrawRangeValid;
using VulkanDetail::IsStridedBufferRangeValid;

[[nodiscard]] inline b8 IsGraphicsPipelineReady(GraphicsPipeline& pipeline, const Device& device)noexcept{
    return pipeline.getDeviceGeneration() == device.getDeviceGeneration() && pipeline.getNativeHandle(ObjectTypes::s_Pipeline).integer != 0u && pipeline.pipelineLayout != VK_NULL_HANDLE;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

