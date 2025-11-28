// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "engine.h"

#include <core/alloc/alloc.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const char* VulkanEngine::s_requestedExtensions[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    SurfaceInstanceName(),
#if defined(VULKAN_VALIDATE)
    VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VulkanEngine::VulkanEngine(Graphics* parent)
    : m_parent(*parent)
    , m_gpuTimestamps(parent->m_persistentArena)
    , m_buffers(parent->m_persistentArena)
    , m_textures(parent->m_persistentArena)
    , m_pipelines(parent->m_persistentArena)
    , m_samplers(parent->m_persistentArena)
    , m_descriptorSetLayouts(parent->m_persistentArena)
    , m_descriptorSets(parent->m_persistentArena)
    , m_renderPasses(parent->m_persistentArena)
    , m_commandBuffers(parent->m_persistentArena)
    , m_shaderStates(parent->m_persistentArena)
    , m_vkInstance(VK_NULL_HANDLE, __hidden_vulkan::VkInstanceDeleter(&m_vkAllocCallbacks))
    , m_vkPhysicalDevice(VK_NULL_HANDLE, __hidden_vulkan::VkPhysicalDeviceRefDeleter())
    , m_vkLogicalDevice(VK_NULL_HANDLE, __hidden_vulkan::VkLogicalDeviceDeleter(&m_vkAllocCallbacks))
    , m_vkQueue(VK_NULL_HANDLE, __hidden_vulkan::VkQueueRefDeleter())
    , m_vkDescriptorPool(__hidden_vulkan::VkDescriptorPoolPtr(nullptr, __hidden_vulkan::VkDescriptorPoolDeleter(&m_vkAllocCallbacks, &m_vkLogicalDevice)))
    , m_vkBindlessDescriptorSetLayout(__hidden_vulkan::VkDescriptorSetLayoutPtr(nullptr, __hidden_vulkan::VkDescriptorSetLayoutDeleter(&m_vkAllocCallbacks, &m_vkLogicalDevice)))
    , m_vkBindlessDescriptorPool(__hidden_vulkan::VkDescriptorPoolPtr(nullptr, __hidden_vulkan::VkDescriptorPoolDeleter(&m_vkAllocCallbacks, &m_vkLogicalDevice)))
    , m_vkTimestampQueryPool(__hidden_vulkan::VkQueryPoolPtr(nullptr, __hidden_vulkan::VkQueryPoolDeleter(&m_vkAllocCallbacks, &m_vkLogicalDevice)))
    , m_vkWinSurface(VK_NULL_HANDLE, __hidden_vulkan::VkSurfaceDeleter(&m_vkAllocCallbacks, &m_vkInstance))
    , m_vkSwapchain(VK_NULL_HANDLE, __hidden_vulkan::VkSwapchainDeleter(&m_vkAllocCallbacks, &m_vkLogicalDevice))
    , m_vkAllocator(VK_NULL_HANDLE, __hidden_vulkan::VmaAllocatorDeleter())
#if defined(VULKAN_VALIDATE)
    , m_vkDebugMessenger(VK_NULL_HANDLE, __hidden_vulkan::VkDebugUtilsMessengerDeleter(&m_vkAllocCallbacks, &m_vkInstance))
#endif
{
    for(usize i = 0; i < s_maxSwapchainImages; ++i){
        m_vkSwapchainImages[i] = __hidden_vulkan::VkImageRef(VK_NULL_HANDLE, __hidden_vulkan::VkImageRefDeleter());
        m_vkSwapchainImageViews[i] = __hidden_vulkan::VkImageViewPtr(VK_NULL_HANDLE, __hidden_vulkan::VkImageViewDeleter(&m_vkAllocCallbacks, &m_vkLogicalDevice));
        m_vkSwapchainFrameBuffers[i] = __hidden_vulkan::VkFramebufferPtr(VK_NULL_HANDLE, __hidden_vulkan::VkFramebufferDeleter(&m_vkAllocCallbacks, &m_vkLogicalDevice));

        m_vkSemaphoreRenderComplete[i] = __hidden_vulkan::VkSemaphorePtr(VK_NULL_HANDLE, __hidden_vulkan::VkSemaphoreDeleter(&m_vkAllocCallbacks, &m_vkLogicalDevice));
        m_vkSemaphoreImageAcquire[i] = __hidden_vulkan::VkSemaphorePtr(VK_NULL_HANDLE, __hidden_vulkan::VkSemaphoreDeleter(&m_vkAllocCallbacks, &m_vkLogicalDevice));
        m_vkFenceCommandBufferExecuted[i] = __hidden_vulkan::VkFencePtr(VK_NULL_HANDLE, __hidden_vulkan::VkFenceDeleter(&m_vkAllocCallbacks, &m_vkLogicalDevice));
    }
}
VulkanEngine::~VulkanEngine(){ destroy(); }

bool VulkanEngine::init(const Common::FrameData& data){
    const char* EngineName = "NWB";
    const char* AppName = "NWBLoader";

    Alloc::ScratchArena tmpArena;
    VkResult err;

#if defined(VULKAN_VALIDATE)
    {
        u32 layerCount = 0;
        ScratchUniquePtr<VkLayerProperties[]> layerProps;
        {
            err = vkEnumerateInstanceLayerProperties(reinterpret_cast<uint32_t*>(&layerCount), nullptr);
            if(err != VK_SUCCESS){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to get required instance layers: {}"), StringConvert(VulkanResultString(err)));
                return false;
            }

            layerProps = MakeScratchUnique<VkLayerProperties[]>(tmpArena, layerCount);
            err = vkEnumerateInstanceLayerProperties(reinterpret_cast<uint32_t*>(&layerCount), layerProps.get());
            if(err != VK_SUCCESS){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to get required instance layers: {}"), StringConvert(VulkanResultString(err)));
                return false;
            }
        }
        for(usize i = 0; i < LengthOf(s_validationLayerName); ++i){
            bool bFound = false;
            for(auto j = decltype(layerCount){ 0 }; j < layerCount; ++j){
                if(NWB_STRCMP(layerProps[j].layerName, s_validationLayerName[i]) == 0){
                    bFound = true;
                    break;
                }
            }
            if(!bFound){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to find required instance layer: {}"), StringConvert(s_validationLayerName[i]));
                return false;
            }
        }
    }
#endif

    {
        VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
        {
            appInfo.pApplicationName = AppName;
            appInfo.applicationVersion = APP_VERSION;
            appInfo.pEngineName = EngineName;
            appInfo.engineVersion = ENGINE_VERSION;
            appInfo.apiVersion = API_VERSION;
        }

        VkInstanceCreateInfo createInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        {
            createInfo.pApplicationInfo = &appInfo;
            createInfo.enabledExtensionCount = static_cast<decltype(createInfo.enabledExtensionCount)>(LengthOf(s_requestedExtensions));
            createInfo.ppEnabledExtensionNames = s_requestedExtensions;
    #if defined(VULKAN_VALIDATE)
            createInfo.enabledLayerCount = static_cast<decltype(createInfo.enabledLayerCount)>(LengthOf(s_validationLayerName));
            createInfo.ppEnabledLayerNames = s_validationLayerName;
    #else
            createInfo.enabledLayerCount = 0;
            createInfo.ppEnabledLayerNames = nullptr;
    #endif
        }
    #if defined(VULKAN_VALIDATE)
        const VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = CreateDebugMessengerInfo();
    #if defined(VULKAN_SYNC_VALIDATE)
        VkValidationFeaturesEXT features{ VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT };
        {
            features.pNext = &debugCreateInfo;
            features.enabledValidationFeatureCount = static_cast<decltype(features.enabledValidationFeatureCount)>(LengthOf(s_validationFeaturesRequested));
            features.pEnabledValidationFeatures = s_validationFeaturesRequested;
        }
        createInfo.pNext = &features;
    #else
        createInfo.pNext = &debugCreateInfo;
    #endif
    #endif

        { // create vulkan instance
            VkInstance object = VK_NULL_HANDLE;
            err = vkCreateInstance(&createInfo, m_vkAllocCallbacks, &object);
            if(err != VK_SUCCESS){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to create Vulkan instance: {}"), StringConvert(VulkanResultString(err)));
                return false;
            }
            m_vkInstance.reset(object);
        }
    }

#if defined(VULKAN_VALIDATE)
    { // choose extension
        u32 extCount = 0;
        ScratchUniquePtr<VkExtensionProperties[]> extProps;

        err = vkEnumerateInstanceExtensionProperties(nullptr, reinterpret_cast<uint32_t*>(&extCount), nullptr);
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to get required instance extensions: {}"), StringConvert(VulkanResultString(err)));
            return false;
        }

        extProps = MakeScratchUnique<VkExtensionProperties[]>(tmpArena, extCount);
        err = vkEnumerateInstanceExtensionProperties(nullptr, reinterpret_cast<uint32_t*>(&extCount), extProps.get());
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to get required instance extensions: {}"), StringConvert(VulkanResultString(err)));
            return false;
        }

        constexpr const char* debugUtilsExtensionName = "VK_EXT_debug_utils";

        for(auto i = decltype(extCount){ 0 }; i < extCount; ++i){
            if(NWB_STRCMP(extProps[i].extensionName, debugUtilsExtensionName) == 0){
                m_debugUtilsExtensionPresents = true;
                break;
            }
        }

        if(m_debugUtilsExtensionPresents){
            auto* vkCreateDebugUtilsMessengerEXT = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_vkInstance.get(), "vkCreateDebugUtilsMessengerEXT"));
            if(vkCreateDebugUtilsMessengerEXT){
                VkDebugUtilsMessengerCreateInfoEXT debugUtilCreateInfo = CreateDebugMessengerInfo();

                VkDebugUtilsMessengerEXT object = VK_NULL_HANDLE;
                err = vkCreateDebugUtilsMessengerEXT(m_vkInstance.get(), &debugUtilCreateInfo, m_vkAllocCallbacks, &object);
                if(err != VK_SUCCESS){
                    NWB_LOGGER_ERROR(NWB_TEXT("Failed to create debug messenger: {}"), StringConvert(VulkanResultString(err)));
                    return false;
                }
                m_vkDebugMessenger.reset(object);
            }
            else{
                NWB_LOGGER_WARNING(NWB_TEXT("Failed to get vkCreateDebugUtilsMessengerEXT function pointer"));
                m_debugUtilsExtensionPresents = false;
            }
        }
        else
            NWB_LOGGER_WARNING(NWB_TEXT("Failed to find required instance extension: {}"), StringConvert(debugUtilsExtensionName));
    }
#endif

    u32 physDevCount = 0;
    ScratchUniquePtr<VkPhysicalDevice[]> physDevs;
    { // choose physical device
        err = vkEnumeratePhysicalDevices(m_vkInstance.get(), reinterpret_cast<uint32_t*>(&physDevCount), nullptr);
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to get physical devices: {}"), StringConvert(VulkanResultString(err)));
            return false;
        }

        physDevs = MakeScratchUnique<VkPhysicalDevice[]>(tmpArena, physDevCount);
        err = vkEnumeratePhysicalDevices(m_vkInstance.get(), reinterpret_cast<uint32_t*>(&physDevCount), physDevs.get());
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to get physical devices: {}"), StringConvert(VulkanResultString(err)));
            return false;
        }
    }

    { // create window surface
        auto* object = CreateSurface(m_vkInstance.get(), data);
        if(object == VK_NULL_HANDLE){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to create Vulkan surface"));
            return false;
        }
        m_vkWinSurface.reset(object);
    }

    { // choose physical device
        auto familyQueue = [this, &tmpArena](VkPhysicalDevice physDev, u32& queueFamily){
            u32 queueFamilyCount = 0;
            ScratchUniquePtr<VkQueueFamilyProperties[]> queueFamilies;

            vkGetPhysicalDeviceQueueFamilyProperties(physDev, reinterpret_cast<uint32_t*>(&queueFamilyCount), nullptr);
            queueFamilies = MakeScratchUnique<VkQueueFamilyProperties[]>(tmpArena, queueFamilyCount);

            vkGetPhysicalDeviceQueueFamilyProperties(physDev, reinterpret_cast<uint32_t*>(&queueFamilyCount), queueFamilies.get());

            VkBool32 output = 0;
            for(auto i = decltype(queueFamilyCount){ 0 }; i < queueFamilyCount; ++i){
                if(queueFamilies[i].queueCount > 0 && queueFamilies[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)){
                    auto err = vkGetPhysicalDeviceSurfaceSupportKHR(physDev, i, m_vkWinSurface.get(), &output);
                    if(err != VK_SUCCESS){
                        output = 0;
                        NWB_LOGGER_WARNING(NWB_TEXT("Failed to get physical device surface support: {}"), StringConvert(VulkanResultString(err)));
                        continue;
                    }

                    if(output){
                        queueFamily = i;
                        break;
                    }
                }
            }
            return output;
        };

        VkPhysicalDevice discreteGPU = VK_NULL_HANDLE;
        u32 discreteQueueFamily = 0;
        VkPhysicalDevice integratedGPU = VK_NULL_HANDLE;
        u32 integratedQueueFamily = 0;

        for(auto i = decltype(physDevCount){ 0 }; i < physDevCount; ++i){
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(physDevs[i], &props);

            u32 queueFamily = 0;
            if(props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU){
                if(familyQueue(physDevs[i], queueFamily)){
                    discreteGPU = physDevs[i];
                    discreteQueueFamily = queueFamily;
                    break;
                }
            }
            else if(props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU){
                if(familyQueue(physDevs[i], queueFamily)){
                    integratedGPU = physDevs[i];
                    integratedQueueFamily = queueFamily;
                    break;
                }
            }
        }

        if(discreteGPU != VK_NULL_HANDLE){
            m_vkPhysicalDevice.reset(discreteGPU);
            m_queueFamily = discreteQueueFamily;
        }
        else if(integratedGPU != VK_NULL_HANDLE){
            m_vkPhysicalDevice.reset(integratedGPU);
            m_queueFamily = integratedQueueFamily;
        }
        else{
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to find suitable physical device"));
            return false;
        }

        vkGetPhysicalDeviceProperties(m_vkPhysicalDevice.get(), &m_vkPhysicalDeviceProperties);
        m_timestampFrequency = m_vkPhysicalDeviceProperties.limits.timestampPeriod / (1000 * 1000);

        m_uboAlignment = static_cast<decltype(m_uboAlignment)>(m_vkPhysicalDeviceProperties.limits.minUniformBufferOffsetAlignment);
        m_ssboAlignment = static_cast<decltype(m_ssboAlignment)>(m_vkPhysicalDeviceProperties.limits.minStorageBufferOffsetAlignment);

        NWB_LOGGER_INFO(NWB_TEXT("GPU selected: {}"), StringConvert(m_vkPhysicalDeviceProperties.deviceName));
    }

    { // create logical device
        VkDeviceQueueCreateInfo queueInfo[1] = {};
        {
            queueInfo[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo[0].queueFamilyIndex = m_queueFamily;
            queueInfo[0].queueCount = static_cast<decltype(VkDeviceQueueCreateInfo::queueCount)>(LengthOf(s_queuePriorities));
            queueInfo[0].pQueuePriorities = s_queuePriorities;
        }

        VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,
            nullptr
        };
        VkPhysicalDeviceFeatures2 physDevFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &indexingFeatures };
        vkGetPhysicalDeviceFeatures2(m_vkPhysicalDevice.get(), &physDevFeatures);

        m_supportBindless = indexingFeatures.descriptorBindingPartiallyBound && indexingFeatures.runtimeDescriptorArray;

        VkDeviceCreateInfo deviceInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        {
            deviceInfo.queueCreateInfoCount = static_cast<decltype(deviceInfo.queueCreateInfoCount)>(LengthOf(queueInfo));
            deviceInfo.pQueueCreateInfos = queueInfo;
            deviceInfo.enabledExtensionCount = static_cast<decltype(deviceInfo.enabledExtensionCount)>(LengthOf(s_deviceExtensions));
            deviceInfo.ppEnabledExtensionNames = s_deviceExtensions;
            deviceInfo.pNext = &physDevFeatures;
        }

        if(m_supportBindless){
            indexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
            indexingFeatures.runtimeDescriptorArray = VK_TRUE;

            physDevFeatures.pNext = &indexingFeatures;
        }

        VkDevice object = VK_NULL_HANDLE;
        err = vkCreateDevice(m_vkPhysicalDevice.get(), &deviceInfo, m_vkAllocCallbacks, &object);
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to create logical device: {}"), StringConvert(VulkanResultString(err)));
            return false;
        }
        m_vkLogicalDevice.reset(object);
    }

#if defined(VULKAN_VALIDATE)
    if(m_debugUtilsExtensionPresents){
        vkFuncSetDebugUtilsObjectNameEXT = reinterpret_cast<decltype(vkFuncSetDebugUtilsObjectNameEXT)>(vkGetInstanceProcAddr(m_vkInstance.get(), "vkSetDebugUtilsObjectNameEXT"));
        vkFuncCmdBeginDebugUtilsLabelEXT = reinterpret_cast<decltype(vkFuncCmdBeginDebugUtilsLabelEXT)>(vkGetInstanceProcAddr(m_vkInstance.get(), "vkCmdBeginDebugUtilsLabelEXT"));
        vkFuncCmdEndDebugUtilsLabelEXT = reinterpret_cast<decltype(vkFuncCmdEndDebugUtilsLabelEXT)>(vkGetInstanceProcAddr(m_vkInstance.get(), "vkCmdEndDebugUtilsLabelEXT"));
    }
#endif

    {
        VkQueue object = VK_NULL_HANDLE;
        vkGetDeviceQueue(m_vkLogicalDevice.get(), m_queueFamily, 0, &object);
        m_vkQueue.reset(object);
    }

    { // choose surface
        u32 supportedCount = 0;
        ScratchUniquePtr<VkSurfaceFormatKHR[]> supportedFormats;

        err = vkGetPhysicalDeviceSurfaceFormatsKHR(m_vkPhysicalDevice.get(), m_vkWinSurface.get(), reinterpret_cast<uint32_t*>(&supportedCount), nullptr);
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to get physical device surface formats: {}"), StringConvert(VulkanResultString(err)));
            return false;
        }

        supportedFormats = MakeScratchUnique<VkSurfaceFormatKHR[]>(tmpArena, supportedCount);
        err = vkGetPhysicalDeviceSurfaceFormatsKHR(m_vkPhysicalDevice.get(), m_vkWinSurface.get(), reinterpret_cast<uint32_t*>(&supportedCount), supportedFormats.get());
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to get physical device surface formats: {}"), StringConvert(VulkanResultString(err)));
            return false;
        }

        m_swapchainOutput.reset();

        bool bFound = false;
        for(auto i = decltype(supportedCount){ 0 }; i < supportedCount; ++i){
            const auto& supportFormat = supportedFormats[i];

            for(usize j = 0; j < LengthOf(s_surfaceFormats); ++j){
                if(supportFormat.format != s_surfaceFormats[j])
                    continue;

                for(usize k = 0; k < LengthOf(s_surfaceColorSpaces); ++k){
                    if(supportFormat.colorSpace != s_surfaceColorSpaces[k])
                        continue;

                    m_vkWinSurfaceFormat = supportFormat;
                    bFound = true;
                    break;
                }

                if(bFound)
                    break;
            }

            if(bFound)
                break;
        }

        if(!bFound){
            m_vkWinSurfaceFormat = supportedFormats[0];
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to find suitable surface format, using default"));
        }

        m_swapchainOutput.addColor(m_vkWinSurfaceFormat.format);
    }

    { // create swapchain
        updatePresentMode(m_parent.m_presentMode);

        if(!createSwapchain())
            return false;
    }

    { // create VMA
        VmaAllocatorCreateInfo createInfo{};
        {
            createInfo.physicalDevice = m_vkPhysicalDevice.get();
            createInfo.device = m_vkLogicalDevice.get();
            createInfo.instance = m_vkInstance.get();
        }

        VmaAllocator object = VK_NULL_HANDLE;
        err = vmaCreateAllocator(&createInfo, &object);
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to create VMA allocator: {}"), StringConvert(VulkanResultString(err)));
            return false;
        }
        m_vkAllocator.reset(object);
    }

    { // Create descriptor pool
        VkDescriptorPoolCreateInfo createInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        {
            createInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            createInfo.maxSets = static_cast<decltype(createInfo.maxSets)>(s_maxGlobalPoolElement * LengthOf(s_globalPoolSizes));
            createInfo.poolSizeCount = static_cast<decltype(createInfo.poolSizeCount)>(LengthOf(s_globalPoolSizes));
            createInfo.pPoolSizes = s_globalPoolSizes;
        }

        VkDescriptorPool object = VK_NULL_HANDLE;
        err = vkCreateDescriptorPool(m_vkLogicalDevice.get(), &createInfo, m_vkAllocCallbacks, &object);
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to create descriptor pool: {}"), StringConvert(VulkanResultString(err)));
            return false;
        }
        m_vkDescriptorPool.reset(object);
    }

    if(m_supportBindless){
        constexpr u32 poolCount = static_cast<decltype(poolCount)>(LengthOf(s_bindlessSizes));

        {
            VkDescriptorPoolCreateInfo createInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            {
                createInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT;
                createInfo.maxSets = static_cast<decltype(createInfo.maxSets)>(s_maxBindlessRes * poolCount);
                createInfo.poolSizeCount = static_cast<decltype(createInfo.poolSizeCount)>(poolCount);
                createInfo.pPoolSizes = s_bindlessSizes;
            }

            VkDescriptorPool object = VK_NULL_HANDLE;
            err = vkCreateDescriptorPool(m_vkLogicalDevice.get(), &createInfo, m_vkAllocCallbacks, &object);
            if(err != VK_SUCCESS){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to create bindless descriptor pool: {}"), StringConvert(VulkanResultString(err)));
                return false;
            }
            m_vkBindlessDescriptorPool.reset(object);
        }

        {
            VkDescriptorSetLayoutBinding bindings[s_numBinding];

            VkDescriptorSetLayoutBinding& bindSampler = bindings[0];
            {
                bindSampler.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                bindSampler.descriptorCount = s_maxBindlessRes;
                bindSampler.binding = s_maxBindlessTex;
                bindSampler.stageFlags = VK_SHADER_STAGE_ALL;
                bindSampler.pImmutableSamplers = nullptr;
            }

            VkDescriptorSetLayoutBinding& bindImage = bindings[1];
            {
                bindImage.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                bindImage.descriptorCount = s_maxBindlessRes;
                bindImage.binding = s_maxBindlessTex + 1;
                bindImage.stageFlags = VK_SHADER_STAGE_ALL;
                bindImage.pImmutableSamplers = nullptr;
            }

            VkDescriptorSetLayoutCreateInfo createInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
            {
                createInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;
                createInfo.bindingCount = static_cast<decltype(createInfo.bindingCount)>(poolCount);
                createInfo.pBindings = bindings;
            }

            VkDescriptorBindingFlags bindngFlags[s_numBinding];
            {
                bindngFlags[0] = s_flagBindless;
                bindngFlags[1] = s_flagBindless;
            }

            VkDescriptorSetLayoutBindingFlagsCreateInfoEXT extendedInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT };
            {
                extendedInfo.bindingCount = static_cast<decltype(extendedInfo.bindingCount)>(poolCount);
                extendedInfo.pBindingFlags = bindngFlags;

                createInfo.pNext = &extendedInfo;
            }

            VkDescriptorSetLayout object = VK_NULL_HANDLE;
            err = vkCreateDescriptorSetLayout(m_vkLogicalDevice.get(), &createInfo, m_vkAllocCallbacks, &object);
            if(err != VK_SUCCESS){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to create bindless descriptor set layout: {}"), StringConvert(VulkanResultString(err)));
                return false;
            }
            m_vkBindlessDescriptorSetLayout.reset(object);
        }

        {
            uint32_t maxBinding = static_cast<decltype(maxBinding)>(s_maxBindlessRes - 1);
            auto* bindlessDescriptorSetLayout = m_vkBindlessDescriptorSetLayout.get();

            VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            {
                allocInfo.descriptorPool = m_vkBindlessDescriptorPool.get();
                allocInfo.descriptorSetCount = 1;
                allocInfo.pSetLayouts = &bindlessDescriptorSetLayout;
            }

            VkDescriptorSetVariableDescriptorCountAllocateInfoEXT extendedInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT };
            {
                extendedInfo.descriptorSetCount = 1;
                extendedInfo.pDescriptorCounts = &maxBinding;

                //allocInfo.pNext = &extendedInfo;
            }

            err = vkAllocateDescriptorSets(m_vkLogicalDevice.get(), &allocInfo, &m_vkBindlessDescriptorSet);
            if(err != VK_SUCCESS){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to allocate bindless descriptor set: {}"), StringConvert(VulkanResultString(err)));
                return false;
            }
        }
    }

    { // create timestamp query pool
        VkQueryPoolCreateInfo createInfo{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
        {
            createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
            createInfo.flags = 0;
            createInfo.queryCount = m_parent.m_gpuTimeQueriesPerFrame * 2 * s_maxFrame;
            createInfo.pipelineStatistics = 0;
            createInfo.pNext = nullptr;
        }

        VkQueryPool object = VK_NULL_HANDLE;
        err = vkCreateQueryPool(m_vkLogicalDevice.get(), &createInfo, m_vkAllocCallbacks, &object);
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to create timestamp query pool: {}"), StringConvert(VulkanResultString(err)));
            return false;
        }
        m_vkTimestampQueryPool.reset(object);
    }

    { // init pools
        m_buffers.init(1024);
        m_textures.init(512);
        m_renderPasses.init(256);
        m_descriptorSetLayouts.init(128);
        m_pipelines.init(128);
        m_shaderStates.init(128);
        m_descriptorSets.init(256);
        m_samplers.init(32);
        //m_commandBuffers.init(128);
    }

    {
        for(usize i = 0; i < s_maxSwapchainImages; ++i){
            {
                VkSemaphoreCreateInfo createInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
                
                VkSemaphore object = VK_NULL_HANDLE;
                err = vkCreateSemaphore(m_vkLogicalDevice.get(), &createInfo, m_vkAllocCallbacks, &object);
                if(err != VK_SUCCESS){
                    NWB_LOGGER_ERROR(NWB_TEXT("Failed to create render complete semaphore: {}"), StringConvert(VulkanResultString(err)));
                    return false;
                }
                m_vkSemaphoreRenderComplete[i].reset(object);
            
                object = VK_NULL_HANDLE;
                err = vkCreateSemaphore(m_vkLogicalDevice.get(), &createInfo, m_vkAllocCallbacks, &object);
                if(err != VK_SUCCESS){
                    NWB_LOGGER_ERROR(NWB_TEXT("Failed to create image acquire semaphore: {}"), StringConvert(VulkanResultString(err)));
                    return false;
                }
                m_vkSemaphoreImageAcquire[i].reset(object);
            }
            {
                VkFenceCreateInfo fenceCreateInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
                {
                    fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
                }

                VkFence object = VK_NULL_HANDLE;
                err = vkCreateFence(m_vkLogicalDevice.get(), &fenceCreateInfo, m_vkAllocCallbacks, &object);
                if(err != VK_SUCCESS){
                    NWB_LOGGER_ERROR(NWB_TEXT("Failed to create command buffer executed fence: {}"), StringConvert(VulkanResultString(err)));
                    return false;
                }
                m_vkFenceCommandBufferExecuted[i].reset(object);
            }
        }

        m_gpuTimestamps.init(m_parent.m_gpuTimeQueriesPerFrame, s_maxFrame);
    }

    return true;
}
void VulkanEngine::destroy(){
    if(m_vkLogicalDevice)
        vkDeviceWaitIdle(m_vkLogicalDevice.get());

    destroySwapchain();
    m_vkWinSurface.reset();

    m_vkAllocator.reset();

#if defined(VULKAN_VALIDATE)
    m_vkDebugMessenger.reset();
#endif

    m_vkBindlessDescriptorPool.reset();
    m_vkBindlessDescriptorSetLayout.reset();

    m_vkDescriptorPool.reset();

    m_vkTimestampQueryPool.reset();

    m_vkQueue.reset();
    m_vkLogicalDevice.reset();
    m_vkPhysicalDevice.reset();
    m_vkInstance.reset();
}


void VulkanEngine::updatePresentMode(PresentMode::Enum mode){
    VkResult err;

    u32 supportedCount = 0;
    static VkPresentModeKHR supportedModes[8];
    {
        err = vkGetPhysicalDeviceSurfacePresentModesKHR(m_vkPhysicalDevice.get(), m_vkWinSurface.get(), reinterpret_cast<uint32_t*>(&supportedCount), nullptr);
        if(err != VK_SUCCESS)
            NWB_LOGGER_WARNING(NWB_TEXT("Failed to get physical device surface present modes: {}"), StringConvert(VulkanResultString(err)));

        NWB_ASSERT(supportedCount < LengthOf(supportedModes));

        err = vkGetPhysicalDeviceSurfacePresentModesKHR(m_vkPhysicalDevice.get(), m_vkWinSurface.get(), reinterpret_cast<uint32_t*>(&supportedCount), supportedModes);
        if(err != VK_SUCCESS)
            NWB_LOGGER_WARNING(NWB_TEXT("Failed to get physical device surface present modes: {}"), StringConvert(VulkanResultString(err)));
    }

    bool bFound = false;
    auto requestedMode = Convert(mode);
    for(auto i = decltype(supportedCount){ 0 }; i < supportedCount; ++i){
        if(requestedMode == supportedModes[i]){
            bFound = true;
            break;
        }
    }

    if(bFound){
        m_vkPresentMode = requestedMode;
        m_parent.m_presentMode = mode;
    }
    else{
        m_vkPresentMode = VK_PRESENT_MODE_FIFO_KHR;
        m_parent.m_presentMode = PresentMode::V_SYNC;
    }
    m_parent.m_swapchainImageCount = (m_vkPresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) ? 2 : 3;
}

void VulkanEngine::destroySwapchain(){
    for(auto i = decltype(m_parent.m_swapchainImageCount){ 0 }; i < m_parent.m_swapchainImageCount; ++i){
        m_vkSwapchainImageViews[i].reset();
        m_vkSwapchainFrameBuffers[i].reset();
        m_vkSwapchainImages[i].reset();
    }
    m_vkSwapchain.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

