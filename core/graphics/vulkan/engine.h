// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <vulkan/vulkan.h>

#include "../common.h"
#include "../graphics.h"

#include "config.h"
#include "utility.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class VulkanEngine{
private:
#if defined(VULKAN_VALIDATE)
    static constexpr const char* s_validationLayerName[] = {
        "VK_LAYER_KHRONOS_validation",
        //"VK_LAYER_LUNARG_core_validation",
        //"VK_LAYER_LUNARG_image",
        //"VK_LAYER_LUNARG_parameter_validation",
        //"VK_LAYER_LUNARG_object_tracker",
    };
#endif
#if defined(VULKAN_SYNC_VALIDATE)
    static constexpr const VkValidationFeatureEnableEXT s_validationFeaturesRequested[] = {
        VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
        //VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
    };
#endif

    static const char* s_requestedExtensions[];

    static constexpr const f32 s_queuePriorities[] = {
        1.f,
    };

    static constexpr const char* s_deviceExtensions[] = {
        "VK_KHR_swapchain",
    };

    static constexpr const VkFormat s_surfaceFormats[] = {
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8_UNORM,
        VK_FORMAT_R8G8B8_UNORM,
    };
    static constexpr const VkColorSpaceKHR s_surfaceColorSpaces[] = {
        VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
    };


public:
    VulkanEngine(Graphics* parent);
    ~VulkanEngine();


public:
    bool init(const Common::FrameData& data);
    void destroy();


private:
    Graphics& m_parent;

private:
    VkAllocationCallbacks* m_allocCallbacks;

private:
    VkInstance m_inst;

    VkPhysicalDevice m_physDev;
    VkPhysicalDeviceProperties m_physDevProps;

    VkDevice m_logiDev;

    VkQueue m_queue;
    u32 m_queueFamilly;

    VkSurfaceKHR m_winSurf;
    VkSurfaceFormatKHR m_winSurfFormat;

    f32 m_timestampFrequency;
    u64 m_uboAlignment;
    u64 m_ssboAlignment;

private:
#if defined(VULKAN_VALIDATE)
    bool m_debugUtilsExtensionPresents;
    VkDebugUtilsMessengerEXT m_debugMessenger;

    PFN_vkSetDebugUtilsObjectNameEXT fnSetDebugUtilsObjectNameEXT;
    PFN_vkCmdBeginDebugUtilsLabelEXT fnCmdBeginDebugUtilsLabelEXT;
    PFN_vkCmdEndDebugUtilsLabelEXT fnCmdEndDebugUtilsLabelEXT;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

