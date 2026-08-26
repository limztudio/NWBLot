// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend_context.h"
#include "backend_context_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BackendContext::createWindowSurface(){
    VkResult res = VK_SUCCESS;

#ifdef NWB_PLATFORM_WINDOWS
    Common::WinFrame frame;
    frame.frameParam() = m_platformFrameParam;

    VkWin32SurfaceCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hinstance = frame.instance();
    createInfo.hwnd = frame.hwnd();

    res = vkCreateWin32SurfaceKHR(m_vulkanInstance, &createInfo, nullptr, &m_windowSurface);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create Win32 surface. {}"), ResultToString(res));
        return false;
    }
    return true;
#elif defined(NWB_PLATFORM_LINUX)
    Common::LinuxFrame frame;
    frame.frameParam() = m_platformFrameParam;

    switch(frame.backend()){
    case Common::LinuxFrameBackend::Enum::X11:
    {
        VkXlibSurfaceCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        createInfo.dpy = reinterpret_cast<decltype(createInfo.dpy)>(frame.nativeDisplay());
        createInfo.window = static_cast<decltype(createInfo.window)>(frame.nativeWindowHandle());

        res = vkCreateXlibSurfaceKHR(m_vulkanInstance, &createInfo, nullptr, &m_windowSurface);
        if(res != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create Xlib surface. {}"), ResultToString(res));
            return false;
        }
        return true;
    }
#if defined(NWB_WITH_WAYLAND)
    case Common::LinuxFrameBackend::Enum::Wayland:
    {
        VkWaylandSurfaceCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        createInfo.display = reinterpret_cast<decltype(createInfo.display)>(frame.nativeDisplay());
        createInfo.surface = reinterpret_cast<decltype(createInfo.surface)>(static_cast<usize>(frame.nativeWindowHandle()));

        res = vkCreateWaylandSurfaceKHR(m_vulkanInstance, &createInfo, nullptr, &m_windowSurface);
        if(res != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create Wayland surface. {}"), ResultToString(res));
            return false;
        }
        return true;
    }
#endif
    case Common::LinuxFrameBackend::Enum::None:
    default:
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Unsupported Linux window backend for surface creation."));
        return false;
    }
#else
    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Surface creation not supported on this platform."));
    return false;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Swap chain management


void BackendContext::destroySwapChain(){
    const bool replacePresentationSemaphore =
        m_framePresentationSignalState == FramePresentationSignalState::Queued
        || m_framePresentationSignalState == FramePresentationSignalState::Accepted
    ;
    VkResult idleResult = VK_SUCCESS;
    if(m_vulkanDevice)
        idleResult = vkDeviceWaitIdle(m_vulkanDevice);
    if(
        replacePresentationSemaphore
        && idleResult == VK_SUCCESS
        && !replaceFramePresentationSemaphoreAfterIdle()
    )
        clearSemaphores(m_presentSemaphores);
    resetFramePresentationSignal();
    m_frameAcquired = false;
    m_frameAbandonmentComplete = false;
    m_swapChainIndex = Limit<u32>::s_Max;

    for(SwapChainImage& swapChainImage : m_swapChainImages){
        if(
            swapChainImage.rhiHandle
            && !swapChainImage.rhiHandle->revokeUnmanagedNativeImage(swapChainImage.image)
        ){
            NWB_LOGGER_CRITICAL_WARNING(
                NWB_TEXT("Vulkan: Failed to revoke a swapchain Texture wrapper before native destruction")
            );
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Swapchain Texture wrapper revocation must succeed"));
        }
    }

    if(m_swapChain){
        vkDestroySwapchainKHR(m_vulkanDevice, m_swapChain, nullptr);
        m_swapChain = VK_NULL_HANDLE;
    }

    for(SwapChainImage& swapChainImage : m_swapChainImages){
        if(swapChainImage.rhiHandle)
            swapChainImage.rhiHandle->releaseRevokedNativeImageIdentity(swapChainImage.image);
    }
    m_swapChainImages.clear();
}

bool BackendContext::createVulkanSwapChain(){
    VkResult res = VK_SUCCESS;

    destroySwapChain();

    const Format::Enum requestedSdrFormat = VulkanDetail::GetBackBufferFormat(m_deviceParams);
    if(VulkanDetail::ConvertFormat(requestedSdrFormat) == VK_FORMAT_UNDEFINED){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create swapchain: requested SDR back buffer format is unsupported"));
        return false;
    }

    const bool hdr10ExtensionEnabled = m_hdr10ColorSpaceExtensionEnabled;
    const bool hdr10Allowed = m_deviceParams.enableHDR10Output && hdr10ExtensionEnabled;

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_SwapChainPresentModeArena);

    uint32_t surfaceFormatCount = 0u;
    res = vkGetPhysicalDeviceSurfaceFormatsKHR(m_vulkanPhysicalDevice, m_windowSurface, &surfaceFormatCount, nullptr);
    if(res != VK_SUCCESS || surfaceFormatCount == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate surface formats for swapchain creation. {}"), ResultToString(res));
        return false;
    }

    Vector<VkSurfaceFormatKHR, Alloc::ScratchArena> surfaceFormats(surfaceFormatCount, scratchArena);
    res = vkGetPhysicalDeviceSurfaceFormatsKHR(m_vulkanPhysicalDevice, m_windowSurface, &surfaceFormatCount, surfaceFormats.data());
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to retrieve surface formats for swapchain creation. {}"), ResultToString(res));
        return false;
    }

    VulkanDetail::SwapChainSurfaceFormatSelection surfaceFormatSelection;
    if(!VulkanDetail::SelectSurfaceFormat(
        surfaceFormats.data(),
        surfaceFormatCount,
        requestedSdrFormat,
        hdr10Allowed,
        surfaceFormatSelection
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Surface exposes neither HDR10 nor a compatible SDR swapchain format."));
        return false;
    }

    m_swapChainFormat = surfaceFormatSelection.surfaceFormat;
    m_swapChainState.backBufferFormat = surfaceFormatSelection.backBufferFormat;
    m_swapChainState.outputMode = surfaceFormatSelection.outputMode;
    if(m_deviceParams.enableHDR10Output && m_swapChainState.outputMode != SwapChainOutputMode::HDR10){
        const tchar* const reason = hdr10ExtensionEnabled
            ? NWB_TEXT("the active surface does not advertise a HDR10/PQ format")
            : NWB_TEXT("VK_EXT_swapchain_colorspace is unavailable")
        ;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Vulkan: HDR10 presentation unavailable; using SDR because {}."), reason);
    }

    VkSurfaceCapabilitiesKHR surfaceCaps = {};
    res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_vulkanPhysicalDevice, m_windowSurface, &surfaceCaps);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to query surface capabilities. {}"), ResultToString(res));
        return false;
    }

    VkExtent2D extent = {};
    if(surfaceCaps.currentExtent.width != UINT32_MAX && surfaceCaps.currentExtent.height != UINT32_MAX){
        extent = surfaceCaps.currentExtent;
    }
    else{
        extent.width = Max(surfaceCaps.minImageExtent.width, Min(surfaceCaps.maxImageExtent.width, m_swapChainState.backBufferWidth));
        extent.height = Max(surfaceCaps.minImageExtent.height, Min(surfaceCaps.maxImageExtent.height, m_swapChainState.backBufferHeight));
    }
    if(extent.width == 0 || extent.height == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Surface extent is invalid ({}x{})."), extent.width, extent.height);
        return false;
    }

    m_swapChainState.backBufferWidth = extent.width;
    m_swapChainState.backBufferHeight = extent.height;

    uint32_t presentModeCount = 0;
    res = vkGetPhysicalDeviceSurfacePresentModesKHR(m_vulkanPhysicalDevice, m_windowSurface, &presentModeCount, nullptr);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate present mode count. {}"), ResultToString(res));
        return false;
    }

    Vector<VkPresentModeKHR, Alloc::ScratchArena> presentModes(presentModeCount, scratchArena);
    res = vkGetPhysicalDeviceSurfacePresentModesKHR(m_vulkanPhysicalDevice, m_windowSurface, &presentModeCount, presentModes.data());
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate present modes. {}"), ResultToString(res));
        return false;
    }

    const VkPresentModeKHR requestedPresentMode = m_swapChainState.vsyncEnabled ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
    VkPresentModeKHR selectedPresentMode = VK_PRESENT_MODE_FIFO_KHR;
    bool requestedPresentModeFound = false;
    bool fifoPresentModeFound = false;
    for(const auto& mode : presentModes){
        if(mode == requestedPresentMode){
            selectedPresentMode = requestedPresentMode;
            requestedPresentModeFound = true;
            break;
        }
        if(mode == VK_PRESENT_MODE_FIFO_KHR)
            fifoPresentModeFound = true;
    }
    if(!requestedPresentModeFound && !fifoPresentModeFound){
        if(presentModes.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Surface exposes no present modes."));
            return false;
        }
        selectedPresentMode = presentModes[0];
    }

    uint32_t selectedImageCount = Max(m_deviceParams.swapChainBufferCount, surfaceCaps.minImageCount);
    if(surfaceCaps.maxImageCount > 0)
        selectedImageCount = Min(selectedImageCount, surfaceCaps.maxImageCount);

    const VkSurfaceTransformFlagBitsKHR selectedPreTransform = (surfaceCaps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
        ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
        : surfaceCaps.currentTransform
    ;

    VkCompositeAlphaFlagBitsKHR selectedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    bool compositeAlphaFound = false;
    const VkCompositeAlphaFlagBitsKHR compositeAlphaCandidates[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
    };
    for(const auto candidate : compositeAlphaCandidates){
        if(surfaceCaps.supportedCompositeAlpha & candidate){
            selectedCompositeAlpha = candidate;
            compositeAlphaFound = true;
            break;
        }
    }
    if(!compositeAlphaFound){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Surface supports no compatible composite alpha mode."));
        return false;
    }

    uint32_t queueFamilyIndices[VulkanDetail::s_SwapChainQueueFamilyIndexCount] = { static_cast<uint32_t>(m_graphicsQueueFamily), static_cast<uint32_t>(m_presentQueueFamily) };
    uint32_t queueFamilyIndexCount = 1;
    if(m_presentQueueFamily != m_graphicsQueueFamily){
        queueFamilyIndices[queueFamilyIndexCount] = static_cast<uint32_t>(m_presentQueueFamily);
        ++queueFamilyIndexCount;
    }
    const bool enableSwapChainSharing = queueFamilyIndexCount > 1;

    VkSwapchainCreateInfoKHR desc = {};
    desc.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    desc.surface = m_windowSurface;
    desc.minImageCount = selectedImageCount;
    desc.imageFormat = m_swapChainFormat.format;
    desc.imageColorSpace = m_swapChainFormat.colorSpace;
    desc.imageExtent = extent;
    desc.imageArrayLayers = 1;
    desc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    desc.imageSharingMode = enableSwapChainSharing ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
    desc.queueFamilyIndexCount = enableSwapChainSharing ? queueFamilyIndexCount : 0;
    desc.pQueueFamilyIndices = enableSwapChainSharing ? queueFamilyIndices : nullptr;
    desc.preTransform = selectedPreTransform;
    desc.compositeAlpha = selectedCompositeAlpha;
    desc.presentMode = selectedPresentMode;
    desc.clipped = VK_TRUE;
    desc.oldSwapchain = VK_NULL_HANDLE;

    if(m_swapChainMutableFormatSupported)
        desc.flags |= VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR;

    VkFormat imageFormats[s_MaxMutableSwapChainFormats] = { m_swapChainFormat.format, VK_FORMAT_UNDEFINED };
    uint32_t imageFormatCount = 1;
    switch(m_swapChainFormat.format){
    case VK_FORMAT_R8G8B8A8_UNORM: imageFormats[1] = VK_FORMAT_R8G8B8A8_SRGB; imageFormatCount = s_MaxMutableSwapChainFormats; break;
    case VK_FORMAT_R8G8B8A8_SRGB:  imageFormats[1] = VK_FORMAT_R8G8B8A8_UNORM; imageFormatCount = s_MaxMutableSwapChainFormats; break;
    case VK_FORMAT_B8G8R8A8_UNORM: imageFormats[1] = VK_FORMAT_B8G8R8A8_SRGB; imageFormatCount = s_MaxMutableSwapChainFormats; break;
    case VK_FORMAT_B8G8R8A8_SRGB:  imageFormats[1] = VK_FORMAT_B8G8R8A8_UNORM; imageFormatCount = s_MaxMutableSwapChainFormats; break;
    default: break;
    }

    VkImageFormatListCreateInfo imageFormatListCreateInfo = {};
    imageFormatListCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO;
    imageFormatListCreateInfo.viewFormatCount = imageFormatCount;
    imageFormatListCreateInfo.pViewFormats = imageFormats;

    if(m_swapChainMutableFormatSupported)
        desc.pNext = &imageFormatListCreateInfo;

    res = vkCreateSwapchainKHR(m_vulkanDevice, &desc, nullptr, &m_swapChain);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create swap chain. {}"), ResultToString(res));
        return false;
    }

    if(
        m_swapChainState.outputMode == SwapChainOutputMode::HDR10
        && isDeviceExtensionEnabled(VK_EXT_HDR_METADATA_EXTENSION_NAME)
    )
        VulkanDetail::SetHdr10Metadata(m_vulkanDevice, m_swapChain);

    uint32_t imageCount = 0;
    res = vkGetSwapchainImagesKHR(m_vulkanDevice, m_swapChain, &imageCount, nullptr);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to query swap chain image count. {}"), ResultToString(res));
        destroySwapChain();
        return false;
    }

    if(imageCount == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Swap chain reported zero images."));
        destroySwapChain();
        return false;
    }

    Vector<VkImage, Alloc::ScratchArena> images(imageCount, scratchArena);
    res = vkGetSwapchainImagesKHR(m_vulkanDevice, m_swapChain, &imageCount, images.data());
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to retrieve swap chain images. {}"), ResultToString(res));
        destroySwapChain();
        return false;
    }

    m_swapChainImages.reserve(imageCount);
    for(uint32_t imageIndex = 0; imageIndex < imageCount; ++imageIndex){
        const VkImage image = images[imageIndex];
        SwapChainImage sci;
        sci.image = image;

        TextureDesc textureDesc;
        textureDesc.width = m_swapChainState.backBufferWidth;
        textureDesc.height = m_swapChainState.backBufferHeight;
        textureDesc.format = m_swapChainState.backBufferFormat;
        textureDesc.initialState = ResourceStates::Present;
        textureDesc.keepInitialState = true;
        textureDesc.isRenderTarget = true;

        sci.rhiHandle = m_rhiDevice->createHandleForNativeTexture(ObjectTypes::VK_Image, Object(sci.image), textureDesc);
        if(!sci.rhiHandle){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create RHI handle for a swap chain image."));
            destroySwapChain();
            return false;
        }
        sci.rhiHandle->m_imageInfo.usage = desc.imageUsage;
        sci.rhiHandle->m_imageInfo.sharingMode = desc.imageSharingMode;
        sci.rhiHandle->m_imageInfo.flags = (desc.flags & VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR) != 0u
            ? VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT
            : 0u
        ;
        sci.rhiHandle->initializeRetainedSubresourceStates(false);
        m_swapChainImages.push_back(Move(sci));
    }

    m_swapChainIndex = Limit<u32>::s_Max;

    if(m_deviceParams.enableDebugRuntime){
        auto ss = VulkanDetail::MakeScratchStringStream(scratchArena);
        ss << "Vulkan GPU debug: swap chain"
           << "\n    extent: " << extent.width << "x" << extent.height
           << "\n    format: " << VulkanDetail::SwapChainFormatToString(m_swapChainFormat.format)
           << " (" << static_cast<i32>(m_swapChainFormat.format) << ")"
           << "\n    color space: " << VulkanDetail::ColorSpaceToString(m_swapChainFormat.colorSpace)
           << " (" << static_cast<i32>(m_swapChainFormat.colorSpace) << ")"
           << "\n    output mode: " << (m_swapChainState.outputMode == SwapChainOutputMode::HDR10 ? "HDR10/PQ" : "SDR/sRGB")
           << "\n    present mode: " << VulkanDetail::PresentModeToString(selectedPresentMode)
           << " (" << static_cast<i32>(selectedPresentMode) << ")"
           << "\n    requested images: " << m_deviceParams.swapChainBufferCount
           << "\n    created images: " << imageCount
           << "\n    mutable format: " << VulkanDetail::BoolToString(m_swapChainMutableFormatSupported)
           << "\n    queue sharing: " << (enableSwapChainSharing ? "concurrent" : "exclusive")
        ;
        NWB_LOGGER_ESSENTIAL_INFO(StringConvert(ss.str()));
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

