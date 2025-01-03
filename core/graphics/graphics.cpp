// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graphics.h"

#include <logger/client/logger.h>

#include "vulkan/helper.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define ENGINE_VERSION VK_MAKE_VERSION(1, 0, 0)
#define APP_VERSION VK_MAKE_VERSION(1, 0, 0)
#define API_VERSION VK_API_VERSION_1_3


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Graphics::Graphics()
: m_physDev(nullptr)
, m_inst(VK_NULL_HANDLE)
{}
Graphics::~Graphics(){}

bool Graphics::init(u16 width, u16 height){
    constexpr char EngineName[] = "NWB";
    constexpr char AppName[] = "NWBLoader";

    VkResult err;

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
    std::unique_ptr<VkExtensionProperties[]> extProps;
    std::unique_ptr<char*[]> extNames;
    {
        err = vkEnumerateInstanceExtensionProperties(nullptr, reinterpret_cast<uint32_t*>(&extCount), nullptr);
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to get required instance extensions: %s"), convert(helperGetVulkanResultString(err)));
            return false;
        }

        extProps = std::make_unique<VkExtensionProperties[]>(extCount);
        err = vkEnumerateInstanceExtensionProperties(nullptr, reinterpret_cast<uint32_t*>(&extCount), extProps.get());
        if(err != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to get required instance extensions: %s"), convert(helperGetVulkanResultString(err)));
            return false;
        }

        extNames = std::make_unique<char*[]>(extCount);
        for(auto i = decltype(extCount){ 0 }; i < extCount; ++i)
            extNames[i] = extProps[i].extensionName;
    }

    VkInstanceCreateInfo createInfo{};
    {
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = static_cast<decltype(createInfo.enabledExtensionCount)>(extCount);
        createInfo.ppEnabledExtensionNames = extNames.get();
    }

    return true;
}
void Graphics::destroy(){
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

