// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "utility.h"

#include <logger/client/logger.h>

#if defined(NWB_PLATFORM_WINDOWS)
#include <vulkan/vulkan_win32.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#if defined(VULKAN_VALIDATE)


namespace __hidden_vulkan{
    static VkBool32 debugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData){
        (void)types;
        (void)userData;

        NWB_LOGGER_INFO(NWB_TEXT("Vulkan\nMessageID: {}(0x{:x})\nMessage: {}"), StringConvert(callbackData->pMessageIdName), callbackData->messageIdNumber, StringConvert(callbackData->pMessage));

        if(severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT){
            //NWB_SOFTBREAK;
        }
        
        return VK_FALSE;
    }
};


VkDebugUtilsMessengerCreateInfoEXT CreateDebugMessengerInfo(){
    VkDebugUtilsMessengerCreateInfoEXT info{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
    info.pfnUserCallback = __hidden_vulkan::debugMessengerCallback;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
    return info;
}


#endif
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const char* SurfaceInstanceName(){
#if defined(NWB_PLATFORM_WINDOWS)
    return "VK_KHR_win32_surface";
#else defined(NWB_PLATFORM_ANDROID)
    return "VK_KHR_android_surface";
#endif
}

VkSurfaceKHR CreateSurface(VkInstance inst, const Common::FrameData& data){
    VkSurfaceKHR output = VK_NULL_HANDLE;
    
    VkResult err = VK_SUCCESS;
#if defined(NWB_PLATFORM_WINDOWS)
    VkWin32SurfaceCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    {
        createInfo.hwnd = static_cast<const Common::WinFrame&>(data).hwnd();
        createInfo.hinstance = static_cast<const Common::WinFrame&>(data).instance();
    }
    err = vkCreateWin32SurfaceKHR(inst, &createInfo, nullptr, &output);
    if(err != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to create Vulkan surface: {}"), StringConvert(VulkanResultString(err)));
        return VK_NULL_HANDLE;
    }
#else
    static_assert(false, "Unsupported platform");
#endif

    return output;
}

bool CreateSwapchain(
    VkAllocationCallbacks* allocCallbacks,
    VkPhysicalDevice physDev,
    VkDevice logiDev,
    u32 queueFamilly,
    VkSurfaceKHR winSurf,
    VkFormat format,
    VkPresentModeKHR presentMode,
    u16* width,
    u16* height,
    u8* imageCount,
    VkSwapchainKHR* swapchain,
    VkImage (*swapchainImages)[s_maxSwapchainImages],
    VkImageView (*swapchainImageViews)[s_maxSwapchainImages]
)
{
    VkResult err;

    VkBool32 isSupported = VK_FALSE;
    {
        err = vkGetPhysicalDeviceSurfaceSupportKHR(physDev, queueFamilly, winSurf, &isSupported);
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to get physical device surface support: {}"), StringConvert(VulkanResultString(err)));
            return false;
        }
        if(isSupported != VK_TRUE)
            NWB_LOGGER_WARNING(NWB_TEXT("Physical device surface is not supported"));
    }

    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    {
        err = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physDev, winSurf, &surfaceCapabilities);
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to get physical device surface capabilities: {}"), StringConvert(VulkanResultString(err)));
            return false;
        }
    }

    VkExtent2D swapchainExtent{ surfaceCapabilities.currentExtent };
    if(swapchainExtent.width == UINT32_MAX){
        swapchainExtent.width = std::clamp(swapchainExtent.width, surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width);
        swapchainExtent.height = std::clamp(swapchainExtent.height, surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height);
    }

    NWB_LOGGER_INFO(NWB_TEXT("Create swapchain: {}x{} saved {}x{} min image: {}"), swapchainExtent.width, swapchainExtent.height, *width, *height, surfaceCapabilities.minImageCount);

    (*width) = static_cast<u16>(swapchainExtent.width);
    (*height) = static_cast<u16>(swapchainExtent.height);

    {
        VkSwapchainCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
        {
            createInfo.surface = winSurf;
            createInfo.minImageCount = (*imageCount);
            createInfo.imageFormat = format;
            createInfo.imageExtent = swapchainExtent;
            createInfo.clipped = VK_TRUE;
            createInfo.imageArrayLayers = 1;
            createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.preTransform = surfaceCapabilities.currentTransform;
            createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
            createInfo.presentMode = presentMode;
        }

        err = vkCreateSwapchainKHR(logiDev, &createInfo, 0, swapchain);
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to create swapchain: {}"), StringConvert(VulkanResultString(err)));
            return false;
        }
    }

    {
        uint32_t supportedImageCount = 0;
        err = vkGetSwapchainImagesKHR(logiDev, *swapchain, &supportedImageCount, nullptr);
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to get swapchain images: {}"), StringConvert(VulkanResultString(err)));
            return false;
        }

        (*imageCount) = static_cast<u8>(supportedImageCount);

        err = vkGetSwapchainImagesKHR(logiDev, *swapchain, &supportedImageCount, *swapchainImages);
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to get swapchain images: {}"), StringConvert(VulkanResultString(err)));
            return false;
        }

        for(auto i = decltype(supportedImageCount){ 0 }; i < supportedImageCount; ++i){
            VkImageViewCreateInfo createInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            {
                createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                createInfo.format = format;
                createInfo.image = (*swapchainImages)[i];
                createInfo.subresourceRange.levelCount = 1;
                createInfo.subresourceRange.layerCount = 1;
                createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                createInfo.components.r = VK_COMPONENT_SWIZZLE_R;
                createInfo.components.g = VK_COMPONENT_SWIZZLE_G;
                createInfo.components.b = VK_COMPONENT_SWIZZLE_B;
                createInfo.components.a = VK_COMPONENT_SWIZZLE_A;
            }

            err = vkCreateImageView(logiDev, &createInfo, allocCallbacks, &(*swapchainImageViews)[i]);
            if(err != VK_SUCCESS){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to create swapchain image view: {}"), StringConvert(VulkanResultString(err)));
                return false;
            }
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

