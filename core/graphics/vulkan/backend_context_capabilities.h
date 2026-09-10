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
    bool apiSupportsVulkan13 = false;
    bool descriptorBufferExtensionAvailable = false;
    bool dynamicRenderingExtensionAvailable = false;
    bool synchronization2ExtensionAvailable = false;
    bool maintenance4ExtensionAvailable = false;
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


[[nodiscard]] inline RequiredQueueFamilySelection SelectRequiredQueueFamilies(
    const VkQueueFamilyProperties* const queueFamilies,
    const u32 queueFamilyCount,
    const bool enableAsyncComputeLane,
    const bool enableDedicatedTransferQueue
)noexcept{
    RequiredQueueFamilySelection result;
    i32 firstGraphicsFamily = s_InvalidQueueFamilyIndex;
    i32 firstComputeFamily = s_InvalidQueueFamilyIndex;
    i32 firstGraphicsComputeFamily = s_InvalidQueueFamilyIndex;
    i32 firstDedicatedComputeFamily = s_InvalidQueueFamilyIndex;

    for(u32 familyIndex = 0u; familyIndex < queueFamilyCount; ++familyIndex){
        const VkQueueFamilyProperties& family = queueFamilies[familyIndex];
        if(family.queueCount == 0u)
            continue;

        const bool supportsGraphics = (family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u;
        const bool supportsCompute = (family.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0u;
        if(firstGraphicsFamily == s_InvalidQueueFamilyIndex && supportsGraphics)
            firstGraphicsFamily = static_cast<i32>(familyIndex);
        if(firstComputeFamily == s_InvalidQueueFamilyIndex && supportsCompute)
            firstComputeFamily = static_cast<i32>(familyIndex);
        if(firstGraphicsComputeFamily == s_InvalidQueueFamilyIndex && supportsGraphics && supportsCompute)
            firstGraphicsComputeFamily = static_cast<i32>(familyIndex);
        if(firstDedicatedComputeFamily == s_InvalidQueueFamilyIndex && supportsCompute && !supportsGraphics)
            firstDedicatedComputeFamily = static_cast<i32>(familyIndex);
        if(
            enableDedicatedTransferQueue
            && result.dedicatedTransferFamily == s_InvalidQueueFamilyIndex
            && (family.queueFlags & VK_QUEUE_TRANSFER_BIT) != 0u
            && !supportsGraphics
            && !supportsCompute
        )
            result.dedicatedTransferFamily = static_cast<i32>(familyIndex);
    }

    if(firstGraphicsComputeFamily != s_InvalidQueueFamilyIndex){
        result.graphicsFamily = firstGraphicsComputeFamily;
        result.computeFamily = firstGraphicsComputeFamily;
        if(enableAsyncComputeLane)
            result.asyncComputeFamily = firstDedicatedComputeFamily;
    }
    else{
        result.graphicsFamily = firstGraphicsFamily;
        result.computeFamily = firstComputeFamily;
    }

    return result;
}

[[nodiscard]] inline bool HasDeviceExtension(
    const VkExtensionProperties* const extensions,
    const u32 extensionCount,
    const char* const extensionName
)noexcept{
    for(u32 extensionIndex = 0u; extensionIndex < extensionCount; ++extensionIndex){
        if(NWB_STRCMP(extensions[extensionIndex].extensionName, extensionName) == 0)
            return true;
    }
    return false;
}

inline void QueryPhysicalDeviceFeatureSupport(
    const VolkInstanceTable& instanceDispatch,
    const VkPhysicalDevice physicalDevice,
    const PhysicalDeviceFeatureQueryOptions& options,
    PhysicalDeviceFeatureSupport& outSupport
)noexcept{
    outSupport = {};
    outSupport.features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    outSupport.vulkan11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    outSupport.vulkan12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    outSupport.vulkan13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    outSupport.descriptorBuffer.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT;
    outSupport.dynamicRendering.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
    outSupport.synchronization2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES;
    outSupport.maintenance4.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES;

    void* featureChain = nullptr;
    const auto appendFeature = [&featureChain](void* const feature){
        VkBaseOutStructure* const featureBase = reinterpret_cast<VkBaseOutStructure*>(feature);
        featureBase->pNext = reinterpret_cast<VkBaseOutStructure*>(featureChain);
        featureChain = feature;
    };
    appendFeature(&outSupport.vulkan11);
    appendFeature(&outSupport.vulkan12);
    if(options.apiSupportsVulkan13)
        appendFeature(&outSupport.vulkan13);
    else{
        if(options.dynamicRenderingExtensionAvailable)
            appendFeature(&outSupport.dynamicRendering);
        if(options.synchronization2ExtensionAvailable)
            appendFeature(&outSupport.synchronization2);
        if(options.maintenance4ExtensionAvailable)
            appendFeature(&outSupport.maintenance4);
    }
    if(options.descriptorBufferExtensionAvailable)
        appendFeature(&outSupport.descriptorBuffer);

    outSupport.features.pNext = featureChain;
    instanceDispatch.vkGetPhysicalDeviceFeatures2(physicalDevice, &outSupport.features);

    if(options.apiSupportsVulkan13){
        outSupport.dynamicRendering.dynamicRendering = outSupport.vulkan13.dynamicRendering;
        outSupport.synchronization2.synchronization2 = outSupport.vulkan13.synchronization2;
        outSupport.maintenance4.maintenance4 = outSupport.vulkan13.maintenance4;
    }

    // Publish a pointer-free snapshot; query chains must never survive a copy.
    outSupport.features.pNext = nullptr;
    outSupport.vulkan11.pNext = nullptr;
    outSupport.vulkan12.pNext = nullptr;
    outSupport.vulkan13.pNext = nullptr;
    outSupport.descriptorBuffer.pNext = nullptr;
    outSupport.dynamicRendering.pNext = nullptr;
    outSupport.synchronization2.pNext = nullptr;
    outSupport.maintenance4.pNext = nullptr;
}

[[nodiscard]] inline const char* FindMissingMandatoryPhysicalDeviceFeature(
    const PhysicalDeviceFeatureSupport& support
)noexcept{
    const VkPhysicalDeviceFeatures& core = support.features.features;
    if(core.shaderImageGatherExtended != VK_TRUE)
        return "shaderImageGatherExtended";
    if(core.samplerAnisotropy != VK_TRUE)
        return "samplerAnisotropy";
    if(core.tessellationShader != VK_TRUE)
        return "tessellationShader";
    if(core.geometryShader != VK_TRUE)
        return "geometryShader";
    if(core.imageCubeArray != VK_TRUE)
        return "imageCubeArray";
    if(core.shaderInt16 != VK_TRUE)
        return "shaderInt16";
    if(core.depthClamp != VK_TRUE)
        return "depthClamp";
    if(core.fillModeNonSolid != VK_TRUE)
        return "fillModeNonSolid";
    if(core.fragmentStoresAndAtomics != VK_TRUE)
        return "fragmentStoresAndAtomics";
    if(core.dualSrcBlend != VK_TRUE)
        return "dualSrcBlend";
    if(core.vertexPipelineStoresAndAtomics != VK_TRUE)
        return "vertexPipelineStoresAndAtomics";
    if(core.shaderInt64 != VK_TRUE)
        return "shaderInt64";
    if(core.shaderStorageImageWriteWithoutFormat != VK_TRUE)
        return "shaderStorageImageWriteWithoutFormat";
    if(core.shaderStorageImageReadWithoutFormat != VK_TRUE)
        return "shaderStorageImageReadWithoutFormat";
    if(core.independentBlend != VK_TRUE)
        return "independentBlend";
    if(core.fullDrawIndexUint32 != VK_TRUE)
        return "fullDrawIndexUint32";
    if(core.multiDrawIndirect != VK_TRUE)
        return "multiDrawIndirect";
    if(core.drawIndirectFirstInstance != VK_TRUE)
        return "drawIndirectFirstInstance";
    if(support.vulkan11.storageBuffer16BitAccess != VK_TRUE)
        return "storageBuffer16BitAccess";
    if(support.vulkan11.shaderDrawParameters != VK_TRUE)
        return "shaderDrawParameters";
    if(support.vulkan12.bufferDeviceAddress != VK_TRUE)
        return "bufferDeviceAddress";
    if(support.vulkan12.descriptorIndexing != VK_TRUE)
        return "descriptorIndexing";
    if(support.vulkan12.runtimeDescriptorArray != VK_TRUE)
        return "runtimeDescriptorArray";
    if(support.vulkan12.timelineSemaphore != VK_TRUE)
        return "timelineSemaphore";
    if(support.vulkan12.shaderFloat16 != VK_TRUE)
        return "shaderFloat16";
    if(support.vulkan12.shaderSampledImageArrayNonUniformIndexing != VK_TRUE)
        return "shaderSampledImageArrayNonUniformIndexing";
    if(support.vulkan12.shaderStorageBufferArrayNonUniformIndexing != VK_TRUE)
        return "shaderStorageBufferArrayNonUniformIndexing";
    if(support.vulkan12.shaderSubgroupExtendedTypes != VK_TRUE)
        return "shaderSubgroupExtendedTypes";
    if(support.vulkan12.scalarBlockLayout != VK_TRUE)
        return "scalarBlockLayout";
    if(support.dynamicRendering.dynamicRendering != VK_TRUE)
        return "dynamicRendering";
    if(support.synchronization2.synchronization2 != VK_TRUE)
        return "synchronization2";
    if(support.descriptorBuffer.descriptorBuffer != VK_TRUE)
        return "descriptorBuffer";
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

