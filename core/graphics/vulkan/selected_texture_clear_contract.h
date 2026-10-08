// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/graphics/backend_selection/texture_contract_types.h>

#include "texture_clear_contract.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<TextureClearContract> ResolveTextureClearContract(
    const TextureDesc& description,
    const TextureSubresourceSet& subresources,
    TextureClearValueKind::Enum valueKind,
    bool clearDepth,
    bool clearStencil
)noexcept;

[[nodiscard]] inline TextureClearQueueRequirement::Enum TextureClearBoxQueueRequirement(const TextureDesc& description, const TextureSubresourceSet& subresources, const Box& box)noexcept{ return static_cast<TextureClearQueueRequirement::Enum>(VulkanTextureDetail::TextureClearBoxQueueRequirement(description, subresources, box)); }

[[nodiscard]] inline bool TextureClearQueueRequirementSatisfied(const TextureClearQueueRequirement::Enum requirement, const GpuQueueCapability::Mask declaredCapabilities, const GpuQueueCapability::Mask physicalCapabilities)noexcept{ return VulkanTextureDetail::TextureClearQueueRequirementSatisfied(static_cast<VulkanTextureDetail::TextureClearQueueRequirement::Enum>(requirement), declaredCapabilities, physicalCapabilities); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

