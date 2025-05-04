// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "engine.h"

#include <logger/client/logger.h>
#include <core/alloc/alloc.h>

#include "helper.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VulkanEngine::VulkanEngine()
: m_allocCallbacks(nullptr)
, m_inst(VK_NULL_HANDLE)
, m_physDev(nullptr)
, m_queueFamilly(0)
, m_windowSurface(VK_NULL_HANDLE)
, m_timestampFrequency(0)
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
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to get required instance layers: {}"), stringConvert(helperGetVulkanResultString(err)));
                return false;
            }

            layerProps = makeScratchUnique<VkLayerProperties[]>(tmpArena, layerCount);
            err = vkEnumerateInstanceLayerProperties(reinterpret_cast<uint32_t*>(&layerCount), layerProps.get());
            if(err != VK_SUCCESS){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to get required instance layers: {}"), stringConvert(helperGetVulkanResultString(err)));
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

    VkApplicationInfo appInfo{};
    {
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = AppName;
        appInfo.applicationVersion = APP_VERSION;
        appInfo.pEngineName = EngineName;
        appInfo.engineVersion = ENGINE_VERSION;
        appInfo.apiVersion = API_VERSION;
    }

    u32 extCount = 0;
    ScratchUniquePtr<VkExtensionProperties[]> extProps;
    ScratchUniquePtr<char*[]> extNames;
    {
        err = vkEnumerateInstanceExtensionProperties(nullptr, reinterpret_cast<uint32_t*>(&extCount), nullptr);
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to get required instance extensions: {}"), stringConvert(helperGetVulkanResultString(err)));
            return false;
        }

        extProps = makeScratchUnique<VkExtensionProperties[]>(tmpArena, extCount);
        err = vkEnumerateInstanceExtensionProperties(nullptr, reinterpret_cast<uint32_t*>(&extCount), extProps.get());
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to get required instance extensions: {}"), stringConvert(helperGetVulkanResultString(err)));
            return false;
        }

        extNames = makeScratchUnique<char*[]>(tmpArena, extCount);
        for(auto i = decltype(extCount){ 0 }; i < extCount; ++i)
            extNames[i] = extProps[i].extensionName;
    }

    VkInstanceCreateInfo createInfo{};
    {
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = static_cast<decltype(createInfo.enabledExtensionCount)>(extCount);
        createInfo.ppEnabledExtensionNames = extNames.get();
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
    VkValidationFeaturesEXT features{};
    {
        features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        features.pNext = &debugCreateInfo;
        features.enabledValidationFeatureCount = static_cast<decltype(features.enabledValidationFeatureCount)>(LengthOf(s_validationFeaturesRequested));
        features.pEnabledValidationFeatures = s_validationFeaturesRequested;
    }
    createInfo.pNext = &features;
#else
    createInfo.pNext = &debugCreateInfo;
#endif
#endif

    VkInstance instance = nullptr;
    {
        err = vkCreateInstance(&createInfo, m_allocCallbacks, &instance);
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to create Vulkan instance: {}"), stringConvert(helperGetVulkanResultString(err)));
            return false;
        }
    }

    u32 physDevCount = 0;
    ScratchUniquePtr<VkPhysicalDevice[]> physDevs;
    {
        err = vkEnumeratePhysicalDevices(instance, reinterpret_cast<uint32_t*>(&physDevCount), nullptr);
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to get physical devices: {}"), stringConvert(helperGetVulkanResultString(err)));
            return false;
        }

        physDevs = makeScratchUnique<VkPhysicalDevice[]>(tmpArena, physDevCount);
        err = vkEnumeratePhysicalDevices(instance, reinterpret_cast<uint32_t*>(&physDevCount), physDevs.get());
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to get physical devices: {}"), stringConvert(helperGetVulkanResultString(err)));
            return false;
        }
    }

    m_windowSurface = createSurface(instance, data);
    if(m_windowSurface == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to create Vulkan surface"));
        return false;
    }

    VkPhysicalDevice discreteGPU = VK_NULL_HANDLE;
    u32 discreteQueueFamilly = 0;
    VkPhysicalDevice integratedGPU = VK_NULL_HANDLE;
    u32 integratedQueueFamilly = 0;
    {
        auto famillyQueue = [this, &tmpArena](VkPhysicalDevice physDev, u32& queueFamily){
            u32 queueFamilyCount = 0;
            ScratchUniquePtr<VkQueueFamilyProperties[]> queueFamilies;

            vkGetPhysicalDeviceQueueFamilyProperties(physDev, reinterpret_cast<uint32_t*>(&queueFamilyCount), nullptr);
            queueFamilies = makeScratchUnique<VkQueueFamilyProperties[]>(tmpArena, queueFamilyCount);

            vkGetPhysicalDeviceQueueFamilyProperties(physDev, reinterpret_cast<uint32_t*>(&queueFamilyCount), queueFamilies.get());

            VkBool32 output = 0;
            for(auto i = decltype(queueFamilyCount){ 0 }; i < queueFamilyCount; ++i){
                if(queueFamilies[i].queueCount > 0 && queueFamilies[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)){
                    vkGetPhysicalDeviceSurfaceSupportKHR(physDev, i, m_windowSurface, &output);
                    if(output){
                        queueFamily = i;
                        break;
                    }
                }
            }
            return output;
        };

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

        NWB_LOGGER_INFO(NWB_TEXT("GPU selected: {}"), stringConvert(m_physDevProps.deviceName));
    }

    return true;
}
void VulkanEngine::destroy(){
    if(m_inst){
        vkDestroyInstance(m_inst, nullptr);
        m_inst = nullptr;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

