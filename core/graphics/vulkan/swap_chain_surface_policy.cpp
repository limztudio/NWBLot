// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "swap_chain_surface_policy.h"

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


Expected<VulkanDetail::SwapChainSurfaceFormatSelection> SwapChainSurfacePolicy::SelectFormat(const Span<const VkSurfaceFormatKHR> supportedFormats, const Format::Enum requestedSdrFormat, const b8 hdr10Allowed)noexcept{
    VulkanDetail::SwapChainSurfaceFormatSelection selection;
    if(!supportedFormats.data() || supportedFormats.empty())
        return MakeUnexpected(Failure{});

    if(hdr10Allowed){
        for(const VkSurfaceFormatKHR& supported : supportedFormats){
            if(!Supports(supported, VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT))
                continue;

            selection.surfaceFormat = supported;
            selection.surfaceFormat.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
            selection.surfaceFormat.colorSpace = VK_COLOR_SPACE_HDR10_ST2084_EXT;
            selection.backBufferFormat = Format::R10G10B10A2_UNORM;
            selection.outputMode = SwapChainOutputMode::HDR10;
            return selection;
        }
    }

    struct SdrCandidate{
        VkFormat format = VK_FORMAT_UNDEFINED;
        Format::Enum rhiFormat = Format::UNKNOWN;
    };
    const VkFormat requestedVkFormat = VulkanDetail::ConvertFormat(requestedSdrFormat);
    const Array<SdrCandidate, 3> candidates = {{
        { requestedVkFormat, requestedSdrFormat },
        // RGBA8/BGRA8 sRGB are interchangeable presentation fallbacks for the engine's default request.
        { VK_FORMAT_B8G8R8A8_SRGB, Format::BGRA8_UNORM_SRGB },
        { VK_FORMAT_R8G8B8A8_SRGB, Format::RGBA8_UNORM_SRGB },
    }};
    for(const SdrCandidate& candidate : candidates){
        if(candidate.format == VK_FORMAT_UNDEFINED)
            continue;
        // Respect a caller's non-default SDR format. The two portable fallbacks are only for the engine default.
        if(candidate.rhiFormat != requestedSdrFormat && requestedSdrFormat != Format::RGBA8_UNORM_SRGB && requestedSdrFormat != Format::BGRA8_UNORM_SRGB)
            continue;

        for(const VkSurfaceFormatKHR& supported : supportedFormats){
            if(!Supports(supported, candidate.format, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR))
                continue;

            selection.surfaceFormat = supported;
            selection.surfaceFormat.format = candidate.format;
            selection.surfaceFormat.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
            selection.backBufferFormat = candidate.rhiFormat;
            selection.outputMode = SwapChainOutputMode::SDR;
            return selection;
        }
    }

    return MakeUnexpected(Failure{});
}

VkExtent2D SwapChainSurfacePolicy::ResolveExtent(const VkSurfaceCapabilitiesKHR& surfaceCaps, const u32 requestedWidth, const u32 requestedHeight)noexcept{
    if(surfaceCaps.currentExtent.width != UINT32_MAX && surfaceCaps.currentExtent.height != UINT32_MAX)
        return surfaceCaps.currentExtent;

    return VkExtent2D{
        .width = Clamp(requestedWidth, surfaceCaps.minImageExtent.width, surfaceCaps.maxImageExtent.width),
        .height = Clamp(requestedHeight, surfaceCaps.minImageExtent.height, surfaceCaps.maxImageExtent.height),
    };
}

Expected<VkPresentModeKHR> SwapChainSurfacePolicy::SelectPresentMode(const Span<const VkPresentModeKHR> modes, const b8 vsyncEnabled)noexcept{
    const VkPresentModeKHR requested = vsyncEnabled ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
    b8 fifoFound = false;
    for(const VkPresentModeKHR mode : modes){
        if(mode == requested)
            return requested;
        if(mode == VK_PRESENT_MODE_FIFO_KHR)
            fifoFound = true;
    }
    if(fifoFound)
        return VK_PRESENT_MODE_FIFO_KHR;
    if(modes.empty())
        return MakeUnexpected(Failure{});
    return modes[0];
}

Expected<VkCompositeAlphaFlagBitsKHR> SwapChainSurfacePolicy::SelectCompositeAlpha(const VkSurfaceCapabilitiesKHR& surfaceCaps)noexcept{
    const Array<VkCompositeAlphaFlagBitsKHR, 4> candidates = {{
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    }};
    for(const auto candidate : candidates){
        if(surfaceCaps.supportedCompositeAlpha & candidate)
            return candidate;
    }
    return MakeUnexpected(Failure{});
}

u32 SwapChainSurfacePolicy::ImageCount(const VkSurfaceCapabilitiesKHR& surfaceCaps, const u32 requestedCount)noexcept{
    u32 count = Max(requestedCount, surfaceCaps.minImageCount);
    if(surfaceCaps.maxImageCount > 0)
        count = Min(count, surfaceCaps.maxImageCount);
    return count;
}

VkSurfaceTransformFlagBitsKHR SwapChainSurfacePolicy::PreTransform(const VkSurfaceCapabilitiesKHR& surfaceCaps)noexcept{
    return (surfaceCaps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : surfaceCaps.currentTransform;
}


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
