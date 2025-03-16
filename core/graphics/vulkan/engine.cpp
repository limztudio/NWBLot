// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "engine.h"

#include <logger/client/logger.h>
#include <core/alloc/alloc.h>

#include "helper.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define ENGINE_VERSION VK_MAKE_VERSION(1, 0, 0)
#define APP_VERSION VK_MAKE_VERSION(1, 0, 0)
#define API_VERSION VK_API_VERSION_1_3


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VulkanEngine::VulkanEngine()
: m_physDev(nullptr)
, m_inst(VK_NULL_HANDLE)
{}
VulkanEngine::~VulkanEngine(){ destroy(); }

bool VulkanEngine::init(u16 width, u16 height){
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
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to get required instance layers: %s"), stringConvert(helperGetVulkanResultString(err)));
                return false;
            }

            layerProps = makeScratchUnique<VkLayerProperties[]>(tmpArena, layerCount);
            err = vkEnumerateInstanceLayerProperties(reinterpret_cast<uint32_t*>(&layerCount), layerProps.get());
            if(err != VK_SUCCESS){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to get required instance layers: %s"), stringConvert(helperGetVulkanResultString(err)));
                return false;
            }
        }
        for(usize i = 0; i < LengthOf(s_validationLayerName); ++i){
            bool bFound = false;
            for(auto j = decltype(layerCount){ 0 }; j < layerCount; ++j){
                if(strcmp(layerProps[j].layerName, s_validationLayerName[i]) == 0){
                    bFound = true;
                    break;
                }
            }
            if(!bFound){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to find required instance layer: %s"), stringConvert(s_validationLayerName[i]));
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
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to get required instance extensions: %s"), stringConvert(helperGetVulkanResultString(err)));
            return false;
        }

        extProps = makeScratchUnique<VkExtensionProperties[]>(tmpArena, extCount);
        err = vkEnumerateInstanceExtensionProperties(nullptr, reinterpret_cast<uint32_t*>(&extCount), extProps.get());
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to get required instance extensions: %s"), stringConvert(helperGetVulkanResultString(err)));
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

    VkInstance instance = nullptr;
    {
        err = vkCreateInstance(&createInfo, nullptr, &instance);
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to create Vulkan instance: %s"), stringConvert(helperGetVulkanResultString(err)));
            return false;
        }
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


#undef ENGINE_VERSION
#undef APP_VERSION
#undef API_VERSION


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

