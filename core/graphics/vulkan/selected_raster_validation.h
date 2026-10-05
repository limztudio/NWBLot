// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/graphics/backend_selection/raster_types.h>

#include "backend.h"
#include "command_validation.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline VertexInputBinding BuildVertexInputBinding(const VkVertexInputBindingDescription* binding)noexcept{ return binding ? VertexInputBinding{ .slot = binding->binding, .stride = binding->stride, .instanceRate = binding->inputRate == VK_VERTEX_INPUT_RATE_INSTANCE, .valid = true } : VertexInputBinding{}; }

[[nodiscard]] inline VertexInputBinding GetVertexInputBinding(const InputLayout& layout, const u32 index)noexcept{ return BuildVertexInputBinding(layout.getBindingDescription(index)); }

[[nodiscard]] inline bool IsViewportValid(const Viewport& viewport, const Device& device)noexcept{ return VulkanDetail::IsViewportValid(viewport, device.getPhysicalDeviceLimits()); }

[[nodiscard]] inline bool IsImplicitScissorValid(const Viewport& viewport)noexcept{ return VulkanDetail::IsImplicitScissorValid(viewport); }

[[nodiscard]] inline bool IsGraphicsPipelineReady(GraphicsPipeline& pipeline, const Device& device){ return pipeline.getDeviceGeneration() == device.getDeviceGeneration() && pipeline.getNativeHandle(ObjectTypes::s_Pipeline).integer != 0u && pipeline.m_pipelineLayout != VK_NULL_HANDLE; }

[[nodiscard]] constexpr bool IsFramebufferAttachmentSubresourceSetValid(const TextureDesc& description, const TextureSubresourceSet& subresources)noexcept{ return VulkanDetail::IsFramebufferAttachmentSubresourceSetValid(description, subresources); }

[[nodiscard]] constexpr u32 GetIndexElementByteSize(const Format::Enum format)noexcept{ return VulkanDetail::GetIndexElementByteSize(format); }

[[nodiscard]] constexpr bool IsIndexDrawRangeValid(const BufferDesc& description, const u64 offset, const u32 startIndex, const u32 indexCount, const u32 indexElementBytes)noexcept{ return VulkanDetail::IsIndexDrawRangeValid(description, offset, startIndex, indexCount, indexElementBytes); }

[[nodiscard]] constexpr bool IsStridedBufferRangeValid(const BufferDesc& description, const u64 offset, const u32 firstElement, const u32 elementCount, const u32 stride, const u32 requiredBytes)noexcept{ return VulkanDetail::IsStridedBufferRangeValid(description, offset, firstElement, elementCount, stride, requiredBytes); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

