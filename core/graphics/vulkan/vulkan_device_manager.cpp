// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_device_manager.h"

#include <logger/client/logger.h>

#include <sstream>

#ifdef NWB_PLATFORM_WINDOWS
#include <windows.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_GraphicsQueueIndex = 0;
static constexpr u32 s_ComputeQueueIndex = 0;
static constexpr u32 s_TransferQueueIndex = 0;
static constexpr u32 s_PresentQueueIndex = 0;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Set>
static Vector<const char*, Alloc::CustomAllocator<const char*>> StringSetToVector(const Set& set, Alloc::CustomArena& arena){
    Alloc::CustomAllocator<const char*> alloc(arena);
    Vector<const char*, Alloc::CustomAllocator<const char*>> ret(alloc);
    ret.reserve(set.size());
    for(const auto& s : set)
        ret.push_back(s.c_str());
    return ret;
}

template<typename Map>
static Vector<const char*, Alloc::CustomAllocator<const char*>> StringMapKeysToVector(const Map& map, Alloc::CustomArena& arena){
    Alloc::CustomAllocator<const char*> alloc(arena);
    Vector<const char*, Alloc::CustomAllocator<const char*>> ret(alloc);
    ret.reserve(map.size());
    for(const auto& [key, val] : map)
        ret.push_back(key.c_str());
    return ret;
}

template<typename Set>
static auto SetToVector(const Set& set, Alloc::CustomArena& arena){
    using T = typename Set::value_type;
    Alloc::CustomAllocator<T> alloc(arena);
    Vector<T, Alloc::CustomAllocator<T>> ret(alloc);
    ret.reserve(set.size());
    for(const auto& s : set)
        ret.push_back(s);
    return ret;
}


static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
    VkDebugReportFlagsEXT flags,
    VkDebugReportObjectTypeEXT objType,
    uint64_t obj,
    size_t location,
    int32_t code,
    const char* layerPrefix,
    const char* msg,
    void* userData
)
{
    (void)flags;
    (void)objType;
    (void)obj;

    const auto* manager = static_cast<const DeviceManager*>(userData);
    if(manager){
        const auto& ignored = manager->getDeviceParams().ignoredVulkanValidationMessageLocations;
        for(const auto& loc : ignored){
            if(loc == location)
                return VK_FALSE;
        }
    }

    NWB_LOGGER_WARNING(NWB_TEXT("Vulkan validation: [location=0x{:x} code={} layer='{}'] {}"), location, code, StringConvert(layerPrefix), StringConvert(msg));

    return VK_FALSE;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DeviceManager::DeviceManager(const DeviceCreationParameters& params)
    : IDeviceManager(params)
    , m_arena(params.allocator->getObjectArena())
    , m_enabledExtensions(m_arena)
    , m_optionalExtensions(m_arena)
    , m_rayTracingExtensions(0, Hasher<AString>(), EqualTo<AString>(), Alloc::CustomAllocator<Pair<const AString, void*>>(m_arena))
    , m_swapChainImages(Alloc::CustomAllocator<SwapChainImage>(m_arena))
    , m_acquireSemaphores(Alloc::CustomAllocator<VkSemaphore>(m_arena))
    , m_presentSemaphores(Alloc::CustomAllocator<VkSemaphore>(m_arena))
    , m_framesInFlight(std::deque<EventQueryHandle, Alloc::CustomAllocator<EventQueryHandle>>(Alloc::CustomAllocator<EventQueryHandle>(m_arena)))
    , m_queryPool(Alloc::CustomAllocator<EventQueryHandle>(m_arena))
{
    NWB_ASSERT_MSG(params.allocator, NWB_TEXT("DeviceManager requires a valid GraphicsAllocator"));
    initDefaultExtensions();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


IDevice* DeviceManager::getDevice()const{
    return m_rhiDevice.get();
}

const tchar* DeviceManager::getRendererString()const{
    return m_rendererString.c_str();
}

bool DeviceManager::isVulkanInstanceExtensionEnabled(const char* extensionName)const{
    return m_enabledExtensions.instance.find(extensionName) != m_enabledExtensions.instance.end();
}

bool DeviceManager::isVulkanDeviceExtensionEnabled(const char* extensionName)const{
    return m_enabledExtensions.device.find(extensionName) != m_enabledExtensions.device.end();
}

bool DeviceManager::isVulkanLayerEnabled(const char* layerName)const{
    return m_enabledExtensions.layers.find(layerName) != m_enabledExtensions.layers.end();
}

void DeviceManager::getEnabledVulkanInstanceExtensions(Vector<AString>& extensions)const{
    for(const auto& ext : m_enabledExtensions.instance)
        extensions.push_back(ext);
}

void DeviceManager::getEnabledVulkanDeviceExtensions(Vector<AString>& extensions)const{
    for(const auto& [name, _] : m_enabledExtensions.device)
        extensions.push_back(name);
}

void DeviceManager::getEnabledVulkanLayers(Vector<AString>& layers)const{
    for(const auto& ext : m_enabledExtensions.layers)
        layers.push_back(ext);
}

ITexture* DeviceManager::getCurrentBackBuffer(){
    return m_swapChainImages[m_swapChainIndex].rhiHandle.get();
}

ITexture* DeviceManager::getBackBuffer(u32 index){
    if(index < m_swapChainImages.size())
        return m_swapChainImages[index].rhiHandle.get();
    return nullptr;
}

u32 DeviceManager::getCurrentBackBufferIndex(){
    return m_swapChainIndex;
}

u32 DeviceManager::getBackBufferCount(){
    return static_cast<u32>(m_swapChainImages.size());
}

void DeviceManager::resizeSwapChain(){
    if(m_vulkanDevice){
        destroySwapChain();
        createSwapChain();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Instance creation


void DeviceManager::initDefaultExtensions(){
    for(auto name : s_enabledInstanceExts)
        m_enabledExtensions.instance.insert(name);
    for(const auto& e : m_enabledDeviceExts)
        m_enabledExtensions.device.insert({ e.name, e.feature });

    for(auto name : s_optionalInstanceExts)
        m_optionalExtensions.instance.insert(name);
    for(const auto& e : m_optionalDeviceExts)
        m_optionalExtensions.device.insert({ e.name, e.feature });

    for(const auto& e : m_rayTracingExts)
        m_rayTracingExtensions.insert({ e.name, e.feature });
}

bool DeviceManager::createInstance(){
    VkResult res = VK_SUCCESS;

    {
        res = volkInitialize();
        if(res != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to initialize volk. {}"), ResultToString(res));
            return false;
        }
    }

#ifdef NWB_PLATFORM_WINDOWS
    if(!m_deviceParams.headlessDevice){
        m_enabledExtensions.instance.insert(VK_KHR_SURFACE_EXTENSION_NAME);
        m_enabledExtensions.instance.insert(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
    }
#endif

    for(const auto& name : m_deviceParams.requiredVulkanInstanceExtensions)
        m_enabledExtensions.instance.insert(name);
    for(const auto& name : m_deviceParams.optionalVulkanInstanceExtensions)
        m_optionalExtensions.instance.insert(name);

    for(const auto& name : m_deviceParams.requiredVulkanLayers)
        m_enabledExtensions.layers.insert(name);
    for(const auto& name : m_deviceParams.optionalVulkanLayers)
        m_optionalExtensions.layers.insert(name);

    decltype(m_enabledExtensions.instance) requiredExtensions(m_enabledExtensions.instance);

    Alloc::ScratchArena<> scratchArena(32768);

    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    Vector<VkExtensionProperties, Alloc::ScratchAllocator<VkExtensionProperties>> availableExtensions(extensionCount, Alloc::ScratchAllocator<VkExtensionProperties>(scratchArena));
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());

    for(const auto& ext : availableExtensions){
        const AString name = ext.extensionName;
        if(m_optionalExtensions.instance.find(name) != m_optionalExtensions.instance.end())
            m_enabledExtensions.instance.insert(name);
        requiredExtensions.erase(name);
    }

    if(!requiredExtensions.empty()){
        AStringStream ss;
        ss << "Cannot create a Vulkan instance because the following required extension(s) are not supported:";
        for(const auto& ext : requiredExtensions)
            ss << std::endl << "  - " << ext;
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: {}"), StringConvert(ss.str()));
        return false;
    }

    {
        AStringStream ss;
        ss << "Vulkan: Enabled instance extensions:";
        for(const auto& ext : m_enabledExtensions.instance)
            ss << "\n    " << ext;
        NWB_LOGGER_INFO(NWB_TEXT("{}"), StringConvert(ss.str()));
    }

    decltype(m_enabledExtensions.layers) requiredLayers(m_enabledExtensions.layers);

    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    Vector<VkLayerProperties, Alloc::ScratchAllocator<VkLayerProperties>> availableLayers(layerCount, Alloc::ScratchAllocator<VkLayerProperties>(scratchArena));
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for(const auto& layer : availableLayers){
        const AString name = layer.layerName;
        if(m_optionalExtensions.layers.find(name) != m_optionalExtensions.layers.end())
            m_enabledExtensions.layers.insert(name);
        requiredLayers.erase(name);
    }

    if(!requiredLayers.empty()){
        AStringStream ss;
        ss << "Cannot create a Vulkan instance because the following required layer(s) are not supported:";
        for(const auto& ext : requiredLayers)
            ss << std::endl << "  - " << ext;
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: {}"), StringConvert(ss.str()));
        return false;
    }

    {
        AStringStream ss;
        ss << "Vulkan: Enabled layers:";
        for(const auto& layer : m_enabledExtensions.layers)
            ss << "\n    " << layer;
        NWB_LOGGER_INFO(NWB_TEXT("{}"), StringConvert(ss.str()));
    }

    auto instanceExtVec = __hidden_vulkan::StringSetToVector(m_enabledExtensions.instance, m_arena);
    auto layerVec = __hidden_vulkan::StringSetToVector(m_enabledExtensions.layers, m_arena);

    VkApplicationInfo applicationInfo = {};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = s_AppName;
    applicationInfo.applicationVersion = static_cast<uint32_t>(s_AppVersion);
    applicationInfo.pEngineName = s_AppName;
    applicationInfo.engineVersion = static_cast<uint32_t>(s_EngineVersion);

    res = vkEnumerateInstanceVersion(&applicationInfo.apiVersion);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate instance version. {}"), ResultToString(res));
        return false;
    }

    if(applicationInfo.apiVersion < s_MinimumVersion){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: API version {}.{}.{} is too low, at least {}.{}.{} is required."),
            VK_API_VERSION_MAJOR(applicationInfo.apiVersion),
            VK_API_VERSION_MINOR(applicationInfo.apiVersion),
            VK_API_VERSION_PATCH(applicationInfo.apiVersion),
            VK_API_VERSION_MAJOR(s_MinimumVersion),
            VK_API_VERSION_MINOR(s_MinimumVersion),
            VK_API_VERSION_PATCH(s_MinimumVersion)
            );
        return false;
    }

    if(VK_API_VERSION_VARIANT(applicationInfo.apiVersion) != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Unexpected API variant: {}"), VK_API_VERSION_VARIANT(applicationInfo.apiVersion));
        return false;
    }

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &applicationInfo;
    createInfo.enabledLayerCount = static_cast<u32>(layerVec.size());
    createInfo.ppEnabledLayerNames = layerVec.data();
    createInfo.enabledExtensionCount = static_cast<u32>(instanceExtVec.size());
    createInfo.ppEnabledExtensionNames = instanceExtVec.data();

    res = vkCreateInstance(&createInfo, nullptr, &m_vulkanInstance);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create instance. {}"), ResultToString(res));
        return false;
    }

    volkLoadInstance(m_vulkanInstance);

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Debug callback


void DeviceManager::installDebugCallback(){
    auto* createFunc = reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(vkGetInstanceProcAddr(m_vulkanInstance, "vkCreateDebugReportCallbackEXT"));
    if(!createFunc)
        return;

    VkDebugReportCallbackCreateInfoEXT createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
    createInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
    createInfo.pfnCallback = __hidden_vulkan::VulkanDebugCallback;
    createInfo.pUserData = this;

    createFunc(m_vulkanInstance, &createInfo, nullptr, &m_debugReportCallback);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Physical device selection


bool DeviceManager::findQueueFamilies(VkPhysicalDevice physicalDevice){
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);

    Alloc::ScratchArena<> scratchArena;

    Vector<VkQueueFamilyProperties, Alloc::ScratchAllocator<VkQueueFamilyProperties>> props(queueFamilyCount, Alloc::ScratchAllocator<VkQueueFamilyProperties>(scratchArena));
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, props.data());

    m_graphicsQueueFamily = -1;
    m_computeQueueFamily = -1;
    m_transferQueueFamily = -1;
    m_presentQueueFamily = -1;

    for(i32 i = 0; i < static_cast<i32>(props.size()); ++i){
        const auto& queueFamily = props[i];

        if(m_graphicsQueueFamily == -1){
            if(queueFamily.queueCount > 0 && (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT))
                m_graphicsQueueFamily = i;
        }

        if(m_computeQueueFamily == -1){
            if(queueFamily.queueCount > 0 &&
                (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) &&
                !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT))
                m_computeQueueFamily = i;
        }

        if(m_transferQueueFamily == -1){
            if(queueFamily.queueCount > 0 &&
                (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT) &&
                !(queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) &&
                !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT))
                m_transferQueueFamily = i;
        }

#ifdef NWB_PLATFORM_WINDOWS
        if(m_presentQueueFamily == -1){
            if(queueFamily.queueCount > 0){
                VkBool32 supported = vkGetPhysicalDeviceWin32PresentationSupportKHR(physicalDevice, i);
                if(supported)
                    m_presentQueueFamily = i;
            }
        }
#endif
    }

    if(
        m_graphicsQueueFamily == -1 ||
        (m_presentQueueFamily == -1 && !m_deviceParams.headlessDevice) ||
        (m_computeQueueFamily == -1 && m_deviceParams.enableComputeQueue) ||
        (m_transferQueueFamily == -1 && m_deviceParams.enableCopyQueue)
        )
    {
        return false;
    }

    return true;
}

bool DeviceManager::pickPhysicalDevice(){
    VkFormat requestedFormat = __hidden_vulkan::ConvertFormat(m_deviceParams.swapChainFormat);
    VkExtent2D requestedExtent = { m_deviceParams.backBufferWidth, m_deviceParams.backBufferHeight };

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_vulkanInstance, &deviceCount, nullptr);

    Alloc::ScratchArena<> scratchArena(32768);

    Vector<VkPhysicalDevice, Alloc::ScratchAllocator<VkPhysicalDevice>> devices(deviceCount, Alloc::ScratchAllocator<VkPhysicalDevice>(scratchArena));
    vkEnumeratePhysicalDevices(m_vulkanInstance, &deviceCount, devices.data());

    i32 adapterIndex = m_deviceParams.adapterIndex;
    i32 firstDevice = 0;
    i32 lastDevice = static_cast<i32>(devices.size()) - 1;
    if(adapterIndex >= 0){
        if(adapterIndex > lastDevice){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: The specified physical device {} does not exist."), adapterIndex);
            return false;
        }
        firstDevice = adapterIndex;
        lastDevice = adapterIndex;
    }

    AStringStream errorStream;
    errorStream << "Cannot find a Vulkan device that supports all the required extensions and properties.";

    Vector<VkPhysicalDevice, Alloc::ScratchAllocator<VkPhysicalDevice>> discreteGPUs((Alloc::ScratchAllocator<VkPhysicalDevice>(scratchArena)));
    Vector<VkPhysicalDevice, Alloc::ScratchAllocator<VkPhysicalDevice>> otherGPUs((Alloc::ScratchAllocator<VkPhysicalDevice>(scratchArena)));

    for(i32 deviceIndex = firstDevice; deviceIndex <= lastDevice; ++deviceIndex){
        VkPhysicalDevice dev = devices[deviceIndex];
        VkPhysicalDeviceProperties prop;
        vkGetPhysicalDeviceProperties(dev, &prop);

        errorStream << std::endl << prop.deviceName << ":";

        HashSet<AString, Hasher<AString>, EqualTo<AString>, Alloc::ScratchAllocator<AString>> requiredExtensions(0, Hasher<AString>(), EqualTo<AString>(), Alloc::ScratchAllocator<AString>(scratchArena));
        for(const auto& [name, _] : m_enabledExtensions.device)
            requiredExtensions.insert(name);
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
        Vector<VkExtensionProperties, Alloc::ScratchAllocator<VkExtensionProperties>> deviceExtensions(extCount, Alloc::ScratchAllocator<VkExtensionProperties>(scratchArena));
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, deviceExtensions.data());
        for(const auto& ext : deviceExtensions)
            requiredExtensions.erase(AString(ext.extensionName));

        bool deviceIsGood = true;

        if(!requiredExtensions.empty()){
            for(const auto& ext : requiredExtensions)
                errorStream << std::endl << "  - missing " << ext;
            deviceIsGood = false;
        }

        VkPhysicalDeviceFeatures deviceFeatures;
        vkGetPhysicalDeviceFeatures(dev, &deviceFeatures);
        if(!deviceFeatures.samplerAnisotropy){
            errorStream << std::endl << "  - does not support samplerAnisotropy";
            deviceIsGood = false;
        }
        if(!deviceFeatures.textureCompressionBC){
            errorStream << std::endl << "  - does not support textureCompressionBC";
            deviceIsGood = false;
        }

        if(!findQueueFamilies(dev)){
            errorStream << std::endl << "  - does not support the necessary queue types";
            deviceIsGood = false;
        }

        if(deviceIsGood && m_windowSurface){
            VkBool32 surfaceSupported = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, m_presentQueueFamily, m_windowSurface, &surfaceSupported);
            if(!surfaceSupported){
                errorStream << std::endl << "  - does not support the window surface";
                deviceIsGood = false;
            }
            else{
                VkSurfaceCapabilitiesKHR surfaceCaps;
                vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev, m_windowSurface, &surfaceCaps);

                uint32_t fmtCount = 0;
                vkGetPhysicalDeviceSurfaceFormatsKHR(dev, m_windowSurface, &fmtCount, nullptr);
                Vector<VkSurfaceFormatKHR, Alloc::ScratchAllocator<VkSurfaceFormatKHR>> surfaceFmts(fmtCount, Alloc::ScratchAllocator<VkSurfaceFormatKHR>(scratchArena));
                vkGetPhysicalDeviceSurfaceFormatsKHR(dev, m_windowSurface, &fmtCount, surfaceFmts.data());

                if(surfaceCaps.minImageCount > m_deviceParams.swapChainBufferCount ||
                    (surfaceCaps.maxImageCount < m_deviceParams.swapChainBufferCount && surfaceCaps.maxImageCount > 0))
                {
                    errorStream << std::endl << "  - cannot support the requested swap chain image count";
                    deviceIsGood = false;
                }

                if(surfaceCaps.minImageExtent.width > requestedExtent.width ||
                    surfaceCaps.minImageExtent.height > requestedExtent.height ||
                    surfaceCaps.maxImageExtent.width < requestedExtent.width ||
                    surfaceCaps.maxImageExtent.height < requestedExtent.height)
                {
                    errorStream << std::endl << "  - cannot support the requested swap chain size";
                    deviceIsGood = false;
                }

                bool surfaceFormatPresent = false;
                for(const auto& surfaceFmt : surfaceFmts){
                    if(surfaceFmt.format == requestedFormat){
                        surfaceFormatPresent = true;
                        break;
                    }
                }

                if(!surfaceFormatPresent){
                    errorStream << std::endl << "  - does not support the requested swap chain format";
                    deviceIsGood = false;
                }

                VkBool32 canPresent = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(dev, m_graphicsQueueFamily, m_windowSurface, &canPresent);
                if(!canPresent){
                    errorStream << std::endl << "  - cannot present";
                    deviceIsGood = false;
                }
            }
        }

        if(!deviceIsGood)
            continue;

        if(prop.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            discreteGPUs.push_back(dev);
        else
            otherGPUs.push_back(dev);
    }

    if(!discreteGPUs.empty()){
        m_vulkanPhysicalDevice = discreteGPUs[0];
        return true;
    }

    if(!otherGPUs.empty()){
        m_vulkanPhysicalDevice = otherGPUs[0];
        return true;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: {}"), StringConvert(errorStream.str()));
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Logical device creation


bool DeviceManager::createDevice(){
    VkResult res = VK_SUCCESS;

    Alloc::ScratchArena<> scratchArena(32768);

    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(m_vulkanPhysicalDevice, nullptr, &extCount, nullptr);
    Vector<VkExtensionProperties, Alloc::ScratchAllocator<VkExtensionProperties>> deviceExtensions(extCount, Alloc::ScratchAllocator<VkExtensionProperties>(scratchArena));
    vkEnumerateDeviceExtensionProperties(m_vulkanPhysicalDevice, nullptr, &extCount, deviceExtensions.data());

    for(const auto& ext : deviceExtensions){
        const AString name = ext.extensionName;
        auto optIt = m_optionalExtensions.device.find(name);
        if(optIt != m_optionalExtensions.device.end()){
            if(name == VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME && m_deviceParams.headlessDevice)
                continue;
            m_enabledExtensions.device.insert({ name, optIt->second });
        }

        if(m_deviceParams.enableRayTracingExtensions){
            auto rtIt = m_rayTracingExtensions.find(name);
            if(rtIt != m_rayTracingExtensions.end())
                m_enabledExtensions.device.insert({ name, rtIt->second });
        }
    }

    if(!m_deviceParams.headlessDevice)
        m_enabledExtensions.device.insert({ VK_KHR_SWAPCHAIN_EXTENSION_NAME, nullptr });

    VkPhysicalDeviceProperties physicalDeviceProperties;
    vkGetPhysicalDeviceProperties(m_vulkanPhysicalDevice, &physicalDeviceProperties);

#ifdef NWB_UNICODE
    {
        const char* deviceName = physicalDeviceProperties.deviceName;
        usize len = NWB_STRNLEN(deviceName, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE);
        m_rendererString.resize(len);
        for(usize i = 0; i < len; ++i)
            m_rendererString[i] = static_cast<tchar>(deviceName[i]);
    }
#else
    m_rendererString = physicalDeviceProperties.deviceName;
#endif

    {
        AStringStream ss;
        ss << "Vulkan: Enabled device extensions:";
        for(const auto& [name, _] : m_enabledExtensions.device)
            ss << "\n    " << name;
        NWB_LOGGER_INFO(NWB_TEXT("{}"), StringConvert(ss.str()));
    }

    m_swapChainMutableFormatSupported = isVulkanDeviceExtensionEnabled(VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME);

    auto appendToChain = [](void*& pNext, void* feature){
        reinterpret_cast<VkBaseOutStructure*>(feature)->pNext = reinterpret_cast<VkBaseOutStructure*>(pNext);
        pNext = feature;
    };

    void* pNext = nullptr;

    VkPhysicalDeviceFeatures2 physicalDeviceFeatures2 = {};
    physicalDeviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures = {};
    bufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
    appendToChain(pNext, &bufferDeviceAddressFeatures);

    VkPhysicalDeviceMaintenance4Features maintenance4Features = {};
    maintenance4Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES;
    if(isVulkanDeviceExtensionEnabled(VK_KHR_MAINTENANCE_4_EXTENSION_NAME))
        appendToChain(pNext, &maintenance4Features);

    physicalDeviceFeatures2.pNext = pNext;
    vkGetPhysicalDeviceFeatures2(m_vulkanPhysicalDevice, &physicalDeviceFeatures2);

    HashSet<i32, Hasher<i32>, EqualTo<i32>, Alloc::ScratchAllocator<i32>> uniqueQueueFamilies(0, Hasher<i32>(), EqualTo<i32>(), Alloc::ScratchAllocator<i32>(scratchArena));
    uniqueQueueFamilies.insert(m_graphicsQueueFamily);

    if(!m_deviceParams.headlessDevice)
        uniqueQueueFamilies.insert(m_presentQueueFamily);
    if(m_deviceParams.enableComputeQueue)
        uniqueQueueFamilies.insert(m_computeQueueFamily);
    if(m_deviceParams.enableCopyQueue)
        uniqueQueueFamilies.insert(m_transferQueueFamily);

    f32 priority = 1.f;
    Vector<VkDeviceQueueCreateInfo, Alloc::ScratchAllocator<VkDeviceQueueCreateInfo>> queueDesc((Alloc::ScratchAllocator<VkDeviceQueueCreateInfo>(scratchArena)));
    queueDesc.reserve(uniqueQueueFamilies.size());
    for(i32 queueFamily : uniqueQueueFamilies){
        VkDeviceQueueCreateInfo queueInfo = {};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = static_cast<u32>(queueFamily);
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        queueDesc.push_back(queueInfo);
    }

    VkPhysicalDeviceVulkan13Features vulkan13features = {};
    vulkan13features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vulkan13features.synchronization2 = isVulkanDeviceExtensionEnabled(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME) ? VK_TRUE : VK_FALSE;
    vulkan13features.maintenance4 = maintenance4Features.maintenance4;

    pNext = nullptr;
    for(const auto& [name, feature] : m_enabledExtensions.device){
        if(feature)
            appendToChain(pNext, feature);
    }

    if(physicalDeviceProperties.apiVersion >= VK_API_VERSION_1_3)
        appendToChain(pNext, &vulkan13features);
    else if(isVulkanDeviceExtensionEnabled(VK_KHR_MAINTENANCE_4_EXTENSION_NAME))
        appendToChain(pNext, &maintenance4Features);

    VkPhysicalDeviceFeatures coreDeviceFeatures = {};
    coreDeviceFeatures.shaderImageGatherExtended = VK_TRUE;
    coreDeviceFeatures.samplerAnisotropy = VK_TRUE;
    coreDeviceFeatures.tessellationShader = VK_TRUE;
    coreDeviceFeatures.textureCompressionBC = VK_TRUE;
    coreDeviceFeatures.geometryShader = VK_TRUE;
    coreDeviceFeatures.imageCubeArray = VK_TRUE;
    coreDeviceFeatures.shaderInt16 = VK_TRUE;
    coreDeviceFeatures.fillModeNonSolid = VK_TRUE;
    coreDeviceFeatures.fragmentStoresAndAtomics = VK_TRUE;
    coreDeviceFeatures.dualSrcBlend = VK_TRUE;
    coreDeviceFeatures.vertexPipelineStoresAndAtomics = VK_TRUE;
    coreDeviceFeatures.shaderInt64 = VK_TRUE;
    coreDeviceFeatures.shaderStorageImageWriteWithoutFormat = VK_TRUE;
    coreDeviceFeatures.shaderStorageImageReadWithoutFormat = VK_TRUE;

    VkPhysicalDeviceVulkan11Features vulkan11features = {};
    vulkan11features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    vulkan11features.storageBuffer16BitAccess = VK_TRUE;
    vulkan11features.pNext = pNext;

    VkPhysicalDeviceVulkan12Features vulkan12features = {};
    vulkan12features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vulkan12features.descriptorIndexing = VK_TRUE;
    vulkan12features.runtimeDescriptorArray = VK_TRUE;
    vulkan12features.descriptorBindingPartiallyBound = VK_TRUE;
    vulkan12features.descriptorBindingVariableDescriptorCount = VK_TRUE;
    vulkan12features.timelineSemaphore = VK_TRUE;
    vulkan12features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    vulkan12features.bufferDeviceAddress = bufferDeviceAddressFeatures.bufferDeviceAddress;
    vulkan12features.shaderSubgroupExtendedTypes = VK_TRUE;
    vulkan12features.scalarBlockLayout = VK_TRUE;
    vulkan12features.pNext = &vulkan11features;

    auto layerVec = __hidden_vulkan::StringSetToVector(m_enabledExtensions.layers, m_arena);
    auto extVec = __hidden_vulkan::StringMapKeysToVector(m_enabledExtensions.device, m_arena);

    VkDeviceCreateInfo deviceCreateInfo = {};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.pQueueCreateInfos = queueDesc.data();
    deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueDesc.size());
    deviceCreateInfo.pEnabledFeatures = &coreDeviceFeatures;
    deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(extVec.size());
    deviceCreateInfo.ppEnabledExtensionNames = extVec.data();
    deviceCreateInfo.enabledLayerCount = static_cast<uint32_t>(layerVec.size());
    deviceCreateInfo.ppEnabledLayerNames = layerVec.data();
    deviceCreateInfo.pNext = &vulkan12features;

    res = vkCreateDevice(m_vulkanPhysicalDevice, &deviceCreateInfo, nullptr, &m_vulkanDevice);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create logical device. {}"), ResultToString(res));
        return false;
    }

    volkLoadDevice(m_vulkanDevice);

    vkGetDeviceQueue(m_vulkanDevice, static_cast<uint32_t>(m_graphicsQueueFamily), __hidden_vulkan::s_GraphicsQueueIndex, &m_graphicsQueue);
    if(m_deviceParams.enableComputeQueue)
        vkGetDeviceQueue(m_vulkanDevice, static_cast<uint32_t>(m_computeQueueFamily), __hidden_vulkan::s_ComputeQueueIndex, &m_computeQueue);
    if(m_deviceParams.enableCopyQueue)
        vkGetDeviceQueue(m_vulkanDevice, static_cast<uint32_t>(m_transferQueueFamily), __hidden_vulkan::s_TransferQueueIndex, &m_transferQueue);
    if(!m_deviceParams.headlessDevice)
        vkGetDeviceQueue(m_vulkanDevice, static_cast<uint32_t>(m_presentQueueFamily), __hidden_vulkan::s_PresentQueueIndex, &m_presentQueue);

    m_bufferDeviceAddressSupported = vulkan12features.bufferDeviceAddress;

    NWB_LOGGER_INFO(NWB_TEXT("Vulkan: Created device: {}"), m_rendererString);

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Window surface creation


bool DeviceManager::createWindowSurface(){
    VkResult res = VK_SUCCESS;

#ifdef NWB_PLATFORM_WINDOWS
    Common::WinFrame frame;
    frame.frameParam() = m_platformFrameParam;

    VkWin32SurfaceCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hinstance = frame.instance();
    createInfo.hwnd = frame.hwnd();

    res = vkCreateWin32SurfaceKHR(m_vulkanInstance, &createInfo, nullptr, &m_windowSurface);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create Win32 surface. {}"), ResultToString(res));
        return false;
    }
    return true;
#else
    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Surface creation not supported on this platform."));
    return false;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Swap chain management


void DeviceManager::destroySwapChain(){
    if(m_vulkanDevice)
        vkDeviceWaitIdle(m_vulkanDevice);

    if(m_swapChain){
        vkDestroySwapchainKHR(m_vulkanDevice, m_swapChain, nullptr);
        m_swapChain = VK_NULL_HANDLE;
    }

    m_swapChainImages.clear();
}

bool DeviceManager::createSwapChain(){
    VkResult res = VK_SUCCESS;

    destroySwapChain();

    m_swapChainFormat.format = __hidden_vulkan::ConvertFormat(m_deviceParams.swapChainFormat);
    m_swapChainFormat.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

    VkExtent2D extent = { m_deviceParams.backBufferWidth, m_deviceParams.backBufferHeight };

    Alloc::ScratchArena<> scratchArena;

    HashSet<uint32_t, Hasher<uint32_t>, EqualTo<uint32_t>, Alloc::ScratchAllocator<uint32_t>> uniqueQueues(0, Hasher<uint32_t>(), EqualTo<uint32_t>(), Alloc::ScratchAllocator<uint32_t>(scratchArena));
    uniqueQueues.insert(static_cast<uint32_t>(m_graphicsQueueFamily));
    uniqueQueues.insert(static_cast<uint32_t>(m_presentQueueFamily));

    Vector<uint32_t, Alloc::ScratchAllocator<uint32_t>> queues{ Alloc::ScratchAllocator<uint32_t>(scratchArena) };
    queues.reserve(uniqueQueues.size());
    for(const auto& q : uniqueQueues)
        queues.push_back(q);
    const bool enableSwapChainSharing = queues.size() > 1;

    VkSwapchainCreateInfoKHR desc = {};
    desc.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    desc.surface = m_windowSurface;
    desc.minImageCount = m_deviceParams.swapChainBufferCount;
    desc.imageFormat = m_swapChainFormat.format;
    desc.imageColorSpace = m_swapChainFormat.colorSpace;
    desc.imageExtent = extent;
    desc.imageArrayLayers = 1;
    desc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    desc.imageSharingMode = enableSwapChainSharing ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
    desc.queueFamilyIndexCount = enableSwapChainSharing ? static_cast<uint32_t>(queues.size()) : 0;
    desc.pQueueFamilyIndices = enableSwapChainSharing ? queues.data() : nullptr;
    desc.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    desc.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    desc.presentMode = m_deviceParams.vsyncEnabled ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
    desc.clipped = VK_TRUE;
    desc.oldSwapchain = VK_NULL_HANDLE;

    if(m_swapChainMutableFormatSupported)
        desc.flags |= VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR;

    VkFormat imageFormats[2] = { m_swapChainFormat.format, VK_FORMAT_UNDEFINED };
    uint32_t imageFormatCount = 1;
    switch(m_swapChainFormat.format){
    case VK_FORMAT_R8G8B8A8_UNORM: imageFormats[1] = VK_FORMAT_R8G8B8A8_SRGB; imageFormatCount = 2; break;
    case VK_FORMAT_R8G8B8A8_SRGB:  imageFormats[1] = VK_FORMAT_R8G8B8A8_UNORM; imageFormatCount = 2; break;
    case VK_FORMAT_B8G8R8A8_UNORM: imageFormats[1] = VK_FORMAT_B8G8R8A8_SRGB; imageFormatCount = 2; break;
    case VK_FORMAT_B8G8R8A8_SRGB:  imageFormats[1] = VK_FORMAT_B8G8R8A8_UNORM; imageFormatCount = 2; break;
    default: break;
    }

    VkImageFormatListCreateInfo imageFormatListCreateInfo = {};
    imageFormatListCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO;
    imageFormatListCreateInfo.viewFormatCount = imageFormatCount;
    imageFormatListCreateInfo.pViewFormats = imageFormats;

    if(m_swapChainMutableFormatSupported)
        desc.pNext = &imageFormatListCreateInfo;

    res = vkCreateSwapchainKHR(m_vulkanDevice, &desc, nullptr, &m_swapChain);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create swap chain. {}"), ResultToString(res));
        return false;
    }

    uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(m_vulkanDevice, m_swapChain, &imageCount, nullptr);

    Vector<VkImage, Alloc::ScratchAllocator<VkImage>> images(imageCount, Alloc::ScratchAllocator<VkImage>(scratchArena));
    vkGetSwapchainImagesKHR(m_vulkanDevice, m_swapChain, &imageCount, images.data());

    for(auto image : images){
        SwapChainImage sci;
        sci.image = image;

        TextureDesc textureDesc;
        textureDesc.width = m_deviceParams.backBufferWidth;
        textureDesc.height = m_deviceParams.backBufferHeight;
        textureDesc.format = m_deviceParams.swapChainFormat;
        textureDesc.initialState = ResourceStates::Present;
        textureDesc.keepInitialState = true;
        textureDesc.isRenderTarget = true;

        sci.rhiHandle = m_rhiDevice->createHandleForNativeTexture(ObjectTypes::VK_Image, Object(sci.image), textureDesc);
        m_swapChainImages.push_back(sci);
    }

    m_swapChainIndex = 0;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// High-level lifecycle methods


bool DeviceManager::createInstanceInternal(){
    if(m_deviceParams.enableDebugRuntime){
        m_enabledExtensions.instance.insert("VK_EXT_debug_report");
        m_enabledExtensions.layers.insert("VK_LAYER_KHRONOS_validation");
    }

    return createInstance();
}

bool DeviceManager::createDeviceInternal(){
    if(m_deviceParams.enableDebugRuntime)
        installDebugCallback();

    for(const auto& name : m_deviceParams.requiredVulkanDeviceExtensions)
        m_enabledExtensions.device.insert({ name, nullptr });
    for(const auto& name : m_deviceParams.optionalVulkanDeviceExtensions)
        m_optionalExtensions.device.insert({ name, nullptr });

    if(!m_deviceParams.headlessDevice){
        if(m_deviceParams.swapChainFormat == Format::RGBA8_UNORM_SRGB)
            m_deviceParams.swapChainFormat = Format::BGRA8_UNORM_SRGB;
        else if(m_deviceParams.swapChainFormat == Format::RGBA8_UNORM)
            m_deviceParams.swapChainFormat = Format::BGRA8_UNORM;

        if(!createWindowSurface())
            return false;
    }
    if(!pickPhysicalDevice())
        return false;
    if(!findQueueFamilies(m_vulkanPhysicalDevice))
        return false;
    if(!createDevice())
        return false;

    auto vecInstanceExt = __hidden_vulkan::StringSetToVector(m_enabledExtensions.instance, m_arena);
    auto vecDeviceExt = __hidden_vulkan::StringMapKeysToVector(m_enabledExtensions.device, m_arena);

    DeviceDesc deviceDesc = {};
    deviceDesc.instance = m_vulkanInstance;
    deviceDesc.physicalDevice = m_vulkanPhysicalDevice;
    deviceDesc.device = m_vulkanDevice;
    deviceDesc.graphicsQueue = m_graphicsQueue;
    deviceDesc.graphicsQueueIndex = m_graphicsQueueFamily;
    if(m_deviceParams.enableComputeQueue){
        deviceDesc.computeQueue = m_computeQueue;
        deviceDesc.computeQueueIndex = m_computeQueueFamily;
    }
    if(m_deviceParams.enableCopyQueue){
        deviceDesc.transferQueue = m_transferQueue;
        deviceDesc.transferQueueIndex = m_transferQueueFamily;
    }
    deviceDesc.instanceExtensions = vecInstanceExt.data();
    deviceDesc.numInstanceExtensions = vecInstanceExt.size();
    deviceDesc.deviceExtensions = vecDeviceExt.data();
    deviceDesc.numDeviceExtensions = vecDeviceExt.size();
    deviceDesc.bufferDeviceAddressSupported = m_bufferDeviceAddressSupported;
    deviceDesc.aftermathEnabled = m_deviceParams.enableAftermath;
    deviceDesc.logBufferLifetime = m_deviceParams.logBufferLifetime;
    deviceDesc.vulkanLibraryName = m_deviceParams.vulkanLibraryName;
    deviceDesc.systemMemoryAllocator = m_deviceParams.allocator ? &m_deviceParams.allocator->getSystemMemoryAllocator() : nullptr;
    deviceDesc.allocator = m_deviceParams.allocator;

    m_rhiDevice = CreateDevice(deviceDesc);

    return true;
}

bool DeviceManager::createSwapChainInternal(){
    if(!createSwapChain())
        return false;

    usize const numPresentSemaphores = m_swapChainImages.size();
    m_presentSemaphores.reserve(numPresentSemaphores);
    for(u32 i = 0; i < numPresentSemaphores; ++i){
        VkSemaphoreCreateInfo semInfo = {};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkSemaphore sem;
        vkCreateSemaphore(m_vulkanDevice, &semInfo, nullptr, &sem);
        m_presentSemaphores.push_back(sem);
    }

    usize const numAcquireSemaphores = (m_deviceParams.maxFramesInFlight > m_swapChainImages.size())
        ? m_deviceParams.maxFramesInFlight : m_swapChainImages.size();
    m_acquireSemaphores.reserve(numAcquireSemaphores);
    for(u32 i = 0; i < numAcquireSemaphores; ++i){
        VkSemaphoreCreateInfo semInfo = {};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkSemaphore sem;
        vkCreateSemaphore(m_vulkanDevice, &semInfo, nullptr, &sem);
        m_acquireSemaphores.push_back(sem);
    }

    return true;
}

void DeviceManager::destroyDeviceAndSwapChain(){
    if(m_rhiDevice)
        m_rhiDevice->waitForIdle();

    while(!m_framesInFlight.empty())
        m_framesInFlight.pop();
    m_queryPool.clear();

    destroySwapChain();

    for(auto& semaphore : m_presentSemaphores){
        if(semaphore){
            vkDestroySemaphore(m_vulkanDevice, semaphore, nullptr);
            semaphore = VK_NULL_HANDLE;
        }
    }
    m_presentSemaphores.clear();

    for(auto& semaphore : m_acquireSemaphores){
        if(semaphore){
            vkDestroySemaphore(m_vulkanDevice, semaphore, nullptr);
            semaphore = VK_NULL_HANDLE;
        }
    }
    m_acquireSemaphores.clear();

    m_rhiDevice = nullptr;
    m_rendererString.clear();

    if(m_vulkanDevice){
        vkDestroyDevice(m_vulkanDevice, nullptr);
        m_vulkanDevice = VK_NULL_HANDLE;
    }

    if(m_windowSurface){
        NWB_ASSERT(m_vulkanInstance);
        vkDestroySurfaceKHR(m_vulkanInstance, m_windowSurface, nullptr);
        m_windowSurface = VK_NULL_HANDLE;
    }

    if(m_debugReportCallback){
        auto* destroyFunc = reinterpret_cast<PFN_vkDestroyDebugReportCallbackEXT>(vkGetInstanceProcAddr(m_vulkanInstance, "vkDestroyDebugReportCallbackEXT"));
        if(destroyFunc)
            destroyFunc(m_vulkanInstance, m_debugReportCallback, nullptr);
        m_debugReportCallback = VK_NULL_HANDLE;
    }

    if(m_vulkanInstance){
        vkDestroyInstance(m_vulkanInstance, nullptr);
        m_vulkanInstance = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Frame management


bool DeviceManager::beginFrame(){
    VkResult res = VK_SUCCESS;

    const VkSemaphore& semaphore = m_acquireSemaphores[m_acquireSemaphoreIndex];

    for(usize attempt = 0; attempt < s_MaxRetryCountAcquireNextImage; ++attempt){
        res = vkAcquireNextImageKHR(
            m_vulkanDevice,
            m_swapChain,
            UINT64_MAX,
            semaphore,
            VK_NULL_HANDLE,
            &m_swapChainIndex
            );

        if((res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) && attempt < s_MaxRetryCountAcquireNextImage - 1){
            backBufferResizing();

            VkSurfaceCapabilitiesKHR surfaceCaps;
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_vulkanPhysicalDevice, m_windowSurface, &surfaceCaps);

            m_deviceParams.backBufferWidth = surfaceCaps.currentExtent.width;
            m_deviceParams.backBufferHeight = surfaceCaps.currentExtent.height;

            resizeSwapChain();
            backBufferResized();
        }
        else
            break;
    }

    m_acquireSemaphoreIndex = (m_acquireSemaphoreIndex + 1) % static_cast<uint32_t>(m_acquireSemaphores.size());

    if(res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR){
        m_rhiDevice->queueWaitForSemaphore(CommandQueue::Graphics, semaphore, 0);
        return true;
    }

    return false;
}

bool DeviceManager::present(){
    VkResult res = VK_SUCCESS;

    const VkSemaphore& semaphore = m_presentSemaphores[m_swapChainIndex];

    m_rhiDevice->queueSignalSemaphore(CommandQueue::Graphics, semaphore, 0);

    // Force semaphore signal by executing empty command list
    m_rhiDevice->executeCommandLists(nullptr, 0);

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &semaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapChain;
    presentInfo.pImageIndices = &m_swapChainIndex;

    res = vkQueuePresentKHR(m_presentQueue, &presentInfo);
    if(!(res == VK_SUCCESS || res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR))
        return false;
    
    while(m_framesInFlight.size() >= m_deviceParams.maxFramesInFlight){
        auto query = m_framesInFlight.front();
        m_framesInFlight.pop();
        m_rhiDevice->waitEventQuery(query.get());
        m_queryPool.push_back(query);
    }

    EventQueryHandle query;
    if(!m_queryPool.empty()){
        query = m_queryPool.back();
        m_queryPool.pop_back();
    }
    else
        query = m_rhiDevice->createEventQuery();

    m_rhiDevice->resetEventQuery(query.get());
    m_rhiDevice->setEventQuery(query.get(), CommandQueue::Graphics);
    m_framesInFlight.push(query);

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Adapter enumeration


bool DeviceManager::enumerateAdapters(Vector<AdapterInfo>& outAdapters){
    if(!m_vulkanInstance)
        return false;

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_vulkanInstance, &deviceCount, nullptr);

    Alloc::ScratchArena<> scratchArena;

    Vector<VkPhysicalDevice, Alloc::ScratchAllocator<VkPhysicalDevice>> devices(deviceCount, Alloc::ScratchAllocator<VkPhysicalDevice>(scratchArena));
    vkEnumeratePhysicalDevices(m_vulkanInstance, &deviceCount, devices.data());
    outAdapters.clear();

    for(auto* physicalDevice : devices){
        VkPhysicalDeviceProperties2 properties2 = {};
        properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        VkPhysicalDeviceIDProperties idProperties = {};
        idProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
        properties2.pNext = &idProperties;
        vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);

        const auto& properties = properties2.properties;

        AdapterInfo adapterInfo;
        adapterInfo.name = properties.deviceName;
        adapterInfo.vendorID = properties.vendorID;
        adapterInfo.deviceID = properties.deviceID;
        adapterInfo.dedicatedVideoMemory = 0;

        NWB_MEMCPY(adapterInfo.uuid.data(), adapterInfo.uuid.size(), idProperties.deviceUUID, adapterInfo.uuid.size());
        adapterInfo.hasUUID = true;

        if(idProperties.deviceLUIDValid){
            NWB_MEMCPY(adapterInfo.luid.data(), adapterInfo.luid.size(), idProperties.deviceLUID, adapterInfo.luid.size());
            adapterInfo.hasLUID = true;
        }

        VkPhysicalDeviceMemoryProperties memoryProperties;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
        for(uint32_t heapIndex = 0; heapIndex < memoryProperties.memoryHeapCount; ++heapIndex){
            const VkMemoryHeap& heap = memoryProperties.memoryHeaps[heapIndex];
            if(heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                adapterInfo.dedicatedVideoMemory += heap.size;
        }

        outAdapters.push_back(Move(adapterInfo));
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

