// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "swap_chain_presentation.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


class SwapChainSurfacePolicy final{
public:
    [[nodiscard]] static Expected<VulkanDetail::SwapChainSurfaceFormatSelection> selectFormat(Span<const VkSurfaceFormatKHR> supportedFormats, Format::Enum requestedSdrFormat, b8 hdr10Allowed)noexcept;
    [[nodiscard]] static VkExtent2D resolveExtent(const VkSurfaceCapabilitiesKHR& surfaceCaps, u32 requestedWidth, u32 requestedHeight)noexcept;
    [[nodiscard]] static Expected<VkPresentModeKHR> selectPresentMode(Span<const VkPresentModeKHR> modes, b8 vsyncEnabled)noexcept;
    [[nodiscard]] static Expected<VkCompositeAlphaFlagBitsKHR> selectCompositeAlpha(const VkSurfaceCapabilitiesKHR& surfaceCaps)noexcept;
    [[nodiscard]] static u32 imageCount(const VkSurfaceCapabilitiesKHR& surfaceCaps, u32 requestedCount)noexcept;
    [[nodiscard]] static VkSurfaceTransformFlagBitsKHR preTransform(const VkSurfaceCapabilitiesKHR& surfaceCaps)noexcept;

private:
    [[nodiscard]] static constexpr b8 supports(const VkSurfaceFormatKHR& supported, VkFormat format, VkColorSpaceKHR colorSpace)noexcept{
        return supported.colorSpace == colorSpace && (supported.format == format || supported.format == VK_FORMAT_UNDEFINED);
    }
};


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
