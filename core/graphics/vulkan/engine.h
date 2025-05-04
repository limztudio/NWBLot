// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <vulkan/vulkan.h>

#include <core/global.h>

#include <core/common/common.h>

#include "config.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class VulkanEngine{
public:
    VulkanEngine();
    ~VulkanEngine();


public:
    bool init(const Common::FrameData& data);
    void destroy();


private:
    VkAllocationCallbacks* m_allocCallbacks;

private:
    VkPhysicalDevice m_physDev;
    VkPhysicalDeviceProperties m_physDevProps;
    u32 m_queueFamilly;
    VkSurfaceKHR m_windowSurface;
    VkInstance m_inst;


private:
#if defined(VULKAN_VALIDATE)
    static constexpr const char* s_validationLayerName[] = {
        "VK_LAYER_KHRONOS_validation"
    };
#endif
#if defined(VULKAN_SYNC_VALIDATE)
    static constexpr const VkValidationFeatureEnableEXT s_validationFeaturesRequested[] = {
        VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
        //VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
    };
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

