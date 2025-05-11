// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "engine.h"

#include <logger/client/logger.h>
#include <core/alloc/alloc.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const char* VulkanEngine::s_requestedExtensions[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    surfaceInstanceName(),
#if defined(VULKAN_VALIDATE)
    VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VulkanEngine::VulkanEngine(Graphics* parent)
    : m_parent(*parent)
    , m_allocCallbacks(nullptr)
    , m_inst(VK_NULL_HANDLE)
    , m_physDev(VK_NULL_HANDLE)
    , m_physDevProps{}
    , m_logiDev(VK_NULL_HANDLE)
    , m_queue(VK_NULL_HANDLE)
    , m_queueFamilly(0)
    , m_swapchainImages{ VK_NULL_HANDLE, }
    , m_swapchainImageViews{ VK_NULL_HANDLE, }
    , m_swapchainFrameBuffers{ VK_NULL_HANDLE, }
    , m_winSurf(VK_NULL_HANDLE)
    , m_winSurfFormat(VK_FORMAT_UNDEFINED)
    , m_presentMode(VK_PRESENT_MODE_FIFO_KHR)
    , m_swapchain(VK_NULL_HANDLE)
    , m_allocator(VK_NULL_HANDLE)
    , m_timestampFrequency(0)
    , m_uboAlignment(256)
    , m_ssboAlignment(256)
#if defined(VULKAN_VALIDATE)
    , m_debugUtilsExtensionPresents(false)
    , m_debugMessenger(VK_NULL_HANDLE)
    , fnSetDebugUtilsObjectNameEXT(nullptr)
    , fnCmdBeginDebugUtilsLabelEXT(nullptr)
    , fnCmdEndDebugUtilsLabelEXT(nullptr)
#endif
{}
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
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to get required instance layers: {}"), stringConvert(getVulkanResultString(err)));
                return false;
            }

            layerProps = makeScratchUnique<VkLayerProperties[]>(tmpArena, layerCount);
            err = vkEnumerateInstanceLayerProperties(reinterpret_cast<uint32_t*>(&layerCount), layerProps.get());
            if(err != VK_SUCCESS){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to get required instance layers: {}"), stringConvert(getVulkanResultString(err)));
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
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to find required instance layer: {}"), stringConvert(s_validationLayerName[i]));
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
        const VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = createDebugMessengerInfo();
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
            err = vkCreateInstance(&createInfo, m_allocCallbacks, &m_inst);
            if(err != VK_SUCCESS){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to create Vulkan instance: {}"), stringConvert(getVulkanResultString(err)));
                return false;
            }
        }
    }

#if defined(VULKAN_VALIDATE)
    { // choose extension
        u32 extCount = 0;
        ScratchUniquePtr<VkExtensionProperties[]> extProps;

        err = vkEnumerateInstanceExtensionProperties(nullptr, reinterpret_cast<uint32_t*>(&extCount), nullptr);
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to get required instance extensions: {}"), stringConvert(getVulkanResultString(err)));
            return false;
        }

        extProps = makeScratchUnique<VkExtensionProperties[]>(tmpArena, extCount);
        err = vkEnumerateInstanceExtensionProperties(nullptr, reinterpret_cast<uint32_t*>(&extCount), extProps.get());
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to get required instance extensions: {}"), stringConvert(getVulkanResultString(err)));
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
            PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT = reinterpret_cast<decltype(vkCreateDebugUtilsMessengerEXT)>(vkGetInstanceProcAddr(m_inst, "vkCreateDebugUtilsMessengerEXT"));
            if(vkCreateDebugUtilsMessengerEXT){
                VkDebugUtilsMessengerCreateInfoEXT debugUtilCreateInfo = createDebugMessengerInfo();

                err = vkCreateDebugUtilsMessengerEXT(m_inst, &debugUtilCreateInfo, m_allocCallbacks, &m_debugMessenger);
                if(err != VK_SUCCESS){
                    NWB_LOGGER_ERROR(NWB_TEXT("Failed to create debug messenger: {}"), stringConvert(getVulkanResultString(err)));
                    return false;
                }
            }
            else{
                NWB_LOGGER_WARNING(NWB_TEXT("Failed to get vkCreateDebugUtilsMessengerEXT function pointer"));
                m_debugUtilsExtensionPresents = false;
            }
        }
        else
            NWB_LOGGER_WARNING(NWB_TEXT("Failed to find required instance extension: {}"), stringConvert(debugUtilsExtensionName));
    }
#endif

    u32 physDevCount = 0;
    ScratchUniquePtr<VkPhysicalDevice[]> physDevs;
    {
        err = vkEnumeratePhysicalDevices(m_inst, reinterpret_cast<uint32_t*>(&physDevCount), nullptr);
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to get physical devices: {}"), stringConvert(getVulkanResultString(err)));
            return false;
        }

        physDevs = makeScratchUnique<VkPhysicalDevice[]>(tmpArena, physDevCount);
        err = vkEnumeratePhysicalDevices(m_inst, reinterpret_cast<uint32_t*>(&physDevCount), physDevs.get());
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to get physical devices: {}"), stringConvert(getVulkanResultString(err)));
            return false;
        }
    }

    { // create window surface
        m_winSurf = createSurface(m_inst, data);
        if(m_winSurf == VK_NULL_HANDLE){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to create Vulkan surface"));
            return false;
        }
    }

    { // choose physical device
        auto famillyQueue = [this, &tmpArena](VkPhysicalDevice physDev, u32& queueFamily){
            u32 queueFamilyCount = 0;
            ScratchUniquePtr<VkQueueFamilyProperties[]> queueFamilies;

            vkGetPhysicalDeviceQueueFamilyProperties(physDev, reinterpret_cast<uint32_t*>(&queueFamilyCount), nullptr);
            queueFamilies = makeScratchUnique<VkQueueFamilyProperties[]>(tmpArena, queueFamilyCount);

            vkGetPhysicalDeviceQueueFamilyProperties(physDev, reinterpret_cast<uint32_t*>(&queueFamilyCount), queueFamilies.get());

            VkBool32 output = 0;
            for(auto i = decltype(queueFamilyCount){ 0 }; i < queueFamilyCount; ++i){
                if(queueFamilies[i].queueCount > 0 && queueFamilies[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)){
                    auto err = vkGetPhysicalDeviceSurfaceSupportKHR(physDev, i, m_winSurf, &output);
                    if(err != VK_SUCCESS){
                        output = 0;
                        NWB_LOGGER_WARNING(NWB_TEXT("Failed to get physical device surface support: {}"), stringConvert(getVulkanResultString(err)));
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
        u32 discreteQueueFamilly = 0;
        VkPhysicalDevice integratedGPU = VK_NULL_HANDLE;
        u32 integratedQueueFamilly = 0;

        for(auto i = decltype(physDevCount){ 0 }; i < physDevCount; ++i){
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(physDevs[i], &props);

            u32 queueFamily = 0;
            if(props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU){
                if(famillyQueue(physDevs[i], queueFamily)){
                    discreteGPU = physDevs[i];
                    discreteQueueFamilly = queueFamily;
                    break;
                }
            }
            else if(props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU){
                if(famillyQueue(physDevs[i], queueFamily)){
                    integratedGPU = physDevs[i];
                    integratedQueueFamilly = queueFamily;
                    break;
                }
            }
        }

        if(discreteGPU != VK_NULL_HANDLE){
            m_physDev = discreteGPU;
            m_queueFamilly = discreteQueueFamilly;
        }
        else if(integratedGPU != VK_NULL_HANDLE){
            m_physDev = integratedGPU;
            m_queueFamilly = integratedQueueFamilly;
        }
        else{
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to find suitable physical device"));
            return false;
        }

        vkGetPhysicalDeviceProperties(m_physDev, &m_physDevProps);
        m_timestampFrequency = m_physDevProps.limits.timestampPeriod / (1000 * 1000);

        m_uboAlignment = static_cast<decltype(m_uboAlignment)>(m_physDevProps.limits.minUniformBufferOffsetAlignment);
        m_ssboAlignment = static_cast<decltype(m_ssboAlignment)>(m_physDevProps.limits.minStorageBufferOffsetAlignment);

        NWB_LOGGER_INFO(NWB_TEXT("GPU selected: {}"), stringConvert(m_physDevProps.deviceName));
    }

    { // create logical device
        VkDeviceQueueCreateInfo queueInfo[1] = {};
        {
            queueInfo[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo[0].queueFamilyIndex = m_queueFamilly;
            queueInfo[0].queueCount = static_cast<decltype(VkDeviceQueueCreateInfo::queueCount)>(LengthOf(s_queuePriorities));
            queueInfo[0].pQueuePriorities = s_queuePriorities;
        }

        VkPhysicalDeviceFeatures2 physDevFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        vkGetPhysicalDeviceFeatures2(m_physDev, &physDevFeatures);

        VkDeviceCreateInfo deviceInfo{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        {
            deviceInfo.queueCreateInfoCount = static_cast<decltype(deviceInfo.queueCreateInfoCount)>(LengthOf(queueInfo));
            deviceInfo.pQueueCreateInfos = queueInfo;
            deviceInfo.enabledExtensionCount = static_cast<decltype(deviceInfo.enabledExtensionCount)>(LengthOf(s_deviceExtensions));
            deviceInfo.ppEnabledExtensionNames = s_deviceExtensions;
            deviceInfo.pNext = &physDevFeatures;
        }

        err = vkCreateDevice(m_physDev, &deviceInfo, m_allocCallbacks, &m_logiDev);
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to create logical device: {}"), stringConvert(getVulkanResultString(err)));
            return false;
        }
    }

#if defined(VULKAN_VALIDATE)
    if(m_debugUtilsExtensionPresents){
        fnSetDebugUtilsObjectNameEXT = reinterpret_cast<decltype(fnSetDebugUtilsObjectNameEXT)>(vkGetInstanceProcAddr(m_inst, "vkSetDebugUtilsObjectNameEXT"));
        fnCmdBeginDebugUtilsLabelEXT = reinterpret_cast<decltype(fnCmdBeginDebugUtilsLabelEXT)>(vkGetInstanceProcAddr(m_inst, "vkCmdBeginDebugUtilsLabelEXT"));
        fnCmdEndDebugUtilsLabelEXT = reinterpret_cast<decltype(fnCmdEndDebugUtilsLabelEXT)>(vkGetInstanceProcAddr(m_inst, "vkCmdEndDebugUtilsLabelEXT"));
    }
#endif

    vkGetDeviceQueue(m_logiDev, m_queueFamilly, 0, &m_queue);

    { // choose surface
        u32 supportedCount = 0;
        ScratchUniquePtr<VkSurfaceFormatKHR[]> supportedFormats;

        err = vkGetPhysicalDeviceSurfaceFormatsKHR(m_physDev, m_winSurf, reinterpret_cast<uint32_t*>(&supportedCount), nullptr);
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to get physical device surface formats: {}"), stringConvert(getVulkanResultString(err)));
            return false;
        }

        supportedFormats = makeScratchUnique<VkSurfaceFormatKHR[]>(tmpArena, supportedCount);
        err = vkGetPhysicalDeviceSurfaceFormatsKHR(m_physDev, m_winSurf, reinterpret_cast<uint32_t*>(&supportedCount), supportedFormats.get());
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to get physical device surface formats: {}"), stringConvert(getVulkanResultString(err)));
            return false;
        }

        m_parent.m_swapchainOutput.reset();

        bool bFound = false;
        for(auto i = decltype(supportedCount){ 0 }; i < supportedCount; ++i){
            const auto& supportFormat = supportedFormats[i];

            for(usize j = 0; j < LengthOf(s_surfaceFormats); ++j){
                if(supportFormat.format != s_surfaceFormats[j])
                    continue;

                for(usize k = 0; k < LengthOf(s_surfaceColorSpaces); ++k){
                    if(supportFormat.colorSpace != s_surfaceColorSpaces[k])
                        continue;

                    m_winSurfFormat = supportFormat;
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
            m_winSurfFormat = supportedFormats[0];
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to find suitable surface format, using default"));
        }

        m_parent.m_swapchainOutput.addColor(convert(m_winSurfFormat.format));
    }

    { // create swapchain
        updatePresentMode(m_parent.m_presentMode);

        if(!createSwapchain())
            return false;
    }

    { // create VMA
        VmaAllocatorCreateInfo createInfo{};
        {
            createInfo.physicalDevice = m_physDev;
            createInfo.device = m_logiDev;
            createInfo.instance = m_inst;
        }

        err = vmaCreateAllocator(&createInfo, &m_allocator);
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to create VMA allocator: {}"), stringConvert(getVulkanResultString(err)));
            return false;
        }
    }

    return true;
}
void VulkanEngine::destroy(){
    if(m_logiDev)
        vkDeviceWaitIdle(m_logiDev);

    destroySwapchain();
    if(m_winSurf && m_inst){
        vkDestroySurfaceKHR(m_inst, m_winSurf, m_allocCallbacks);
        m_winSurf = VK_NULL_HANDLE;
    }

    if(m_allocator){
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
    }

#if defined(VULKAN_VALIDATE)
    if(m_debugMessenger && m_inst){
        PFN_vkDestroyDebugUtilsMessengerEXT vkDestroyDebugUtilsMessengerEXT = reinterpret_cast<decltype(vkDestroyDebugUtilsMessengerEXT)>(vkGetInstanceProcAddr(m_inst, "vkDestroyDebugUtilsMessengerEXT"));
        if(vkDestroyDebugUtilsMessengerEXT){
            vkDestroyDebugUtilsMessengerEXT(m_inst, m_debugMessenger, m_allocCallbacks);
            m_debugMessenger = VK_NULL_HANDLE;
        }
        else
            NWB_LOGGER_WARNING(NWB_TEXT("Failed to get vkDestroyDebugUtilsMessengerEXT function pointer"));
    }
#endif

    if(m_logiDev){
        vkDestroyDevice(m_logiDev, m_allocCallbacks);
        m_logiDev = VK_NULL_HANDLE;
    }

    if(m_inst){
        vkDestroyInstance(m_inst, m_allocCallbacks);
        m_inst = nullptr;
    }
}


void VulkanEngine::updatePresentMode(PresentMode mode){
    VkResult err;

    u32 supportedCount = 0;
    static VkPresentModeKHR supportedModes[8];
    {
        err = vkGetPhysicalDeviceSurfacePresentModesKHR(m_physDev, m_winSurf, reinterpret_cast<uint32_t*>(&supportedCount), nullptr);
        if(err != VK_SUCCESS)
            NWB_LOGGER_WARNING(NWB_TEXT("Failed to get physical device surface present modes: {}"), stringConvert(getVulkanResultString(err)));

        assert(supportedCount < LengthOf(supportedModes));

        err = vkGetPhysicalDeviceSurfacePresentModesKHR(m_physDev, m_winSurf, reinterpret_cast<uint32_t*>(&supportedCount), supportedModes);
        if(err != VK_SUCCESS)
            NWB_LOGGER_WARNING(NWB_TEXT("Failed to get physical device surface present modes: {}"), stringConvert(getVulkanResultString(err)));
    }

    bool bFound = false;
    auto requestedMode = convert(mode);
    for(auto i = decltype(supportedCount){ 0 }; i < supportedCount; ++i){
        if(requestedMode == supportedModes[i]){
            bFound = true;
            break;
        }
    }

    if(bFound){
        m_presentMode = requestedMode;
        m_parent.m_presentMode = mode;
    }
    else{
        m_presentMode = VK_PRESENT_MODE_FIFO_KHR;
        m_parent.m_presentMode = PresentMode::VSync;
    }
    m_parent.m_swapchainImageCount = (m_presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) ? 2 : 3;
}

void VulkanEngine::destroySwapchain(){
    for(auto i = decltype(m_parent.m_swapchainImageCount){ 0 }; i < m_parent.m_swapchainImageCount; ++i){
        if(m_swapchainImageViews[i]){
            vkDestroyImageView(m_logiDev, m_swapchainImageViews[i], m_allocCallbacks);
            m_swapchainImageViews[i] = VK_NULL_HANDLE;
        }

        if(m_swapchainFrameBuffers[i]){
            vkDestroyFramebuffer(m_logiDev, m_swapchainFrameBuffers[i], m_allocCallbacks);
            m_swapchainFrameBuffers[i] = VK_NULL_HANDLE;
        }
    }
    if(m_swapchain){
        vkDestroySwapchainKHR(m_logiDev, m_swapchain, m_allocCallbacks);
        m_swapchain = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

