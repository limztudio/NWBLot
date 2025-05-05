// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "helper.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#if defined(VULKAN_VALIDATE)


namespace __hidden_vulkan{
    static VkBool32 debugMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData){
        (void)types;
        (void)userData;

        NWB_LOGGER_INFO(NWB_TEXT("Vulkan\nMessageID: {}(0x{:x})\nMessage: {}"), stringConvert(callbackData->pMessageIdName), callbackData->messageIdNumber, stringConvert(callbackData->pMessage));

        if(severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT){
            //NWB_SOFTBREAK;
        }
        
        return VK_FALSE;
    }
};


VkDebugUtilsMessengerCreateInfoEXT createDebugMessengerInfo(){
    VkDebugUtilsMessengerCreateInfoEXT info{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
    info.pfnUserCallback = __hidden_vulkan::debugMessengerCallback;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
    return info;
}


#endif
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)
#include <vulkan/vulkan_win32.h>
#endif

const char* surfaceInstanceName(){
#if defined(NWB_PLATFORM_WINDOWS)
    return "VK_KHR_win32_surface";
#else defined(NWB_PLATFORM_ANDROID)
    return "VK_KHR_android_surface";
#endif
}

VkSurfaceKHR createSurface(VkInstance inst, const Common::FrameData& data){
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
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to create Vulkan surface: {}"), stringConvert(helperGetVulkanResultString(err)));
        return VK_NULL_HANDLE;
    }
#else
    static_assert(false, "Unsupported platform");
#endif

    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

