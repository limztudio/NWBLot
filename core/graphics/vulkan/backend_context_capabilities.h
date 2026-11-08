// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RequiredQueueFamilySelection{
    i32 graphicsFamily = s_InvalidQueueFamilyIndex;
    i32 computeFamily = s_InvalidQueueFamilyIndex;
    i32 asyncComputeFamily = s_InvalidQueueFamilyIndex;
    i32 dedicatedTransferFamily = s_InvalidQueueFamilyIndex;
};

struct PhysicalDeviceFeatureQueryOptions{
    b8 apiSupportsVulkan13 = false;
    b8 descriptorBufferExtensionAvailable = false;
    b8 dynamicRenderingExtensionAvailable = false;
    b8 synchronization2ExtensionAvailable = false;
    b8 maintenance4ExtensionAvailable = false;
};

struct PhysicalDeviceFeatureSupport{
    VkPhysicalDeviceFeatures2 features = {};
    VkPhysicalDeviceVulkan11Features vulkan11 = {};
    VkPhysicalDeviceVulkan12Features vulkan12 = {};
    VkPhysicalDeviceVulkan13Features vulkan13 = {};
    VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptorBuffer = {};
    VkPhysicalDeviceDynamicRenderingFeatures dynamicRendering = {};
    VkPhysicalDeviceSynchronization2Features synchronization2 = {};
    VkPhysicalDeviceMaintenance4Features maintenance4 = {};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class PhysicalDeviceCapabilities{
public:
    [[nodiscard]] static RequiredQueueFamilySelection SelectQueueFamilies(Span<const VkQueueFamilyProperties> queueFamilies, b8 enableAsyncComputeLane, b8 enableDedicatedTransferQueue)noexcept;
    [[nodiscard]] static b8 HasExtension(Span<const VkExtensionProperties> extensions, AStringView extensionName)noexcept;
    static void QueryFeatureSupport(const VolkInstanceTable& instanceDispatch, VkPhysicalDevice physicalDevice, const PhysicalDeviceFeatureQueryOptions& options, PhysicalDeviceFeatureSupport& outSupport)noexcept;
    [[nodiscard]] static AStringView FindMissingFeature(const PhysicalDeviceFeatureSupport& support)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

