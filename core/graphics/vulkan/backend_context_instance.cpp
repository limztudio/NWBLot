// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend_context.h"
#include "backend_context_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void BackendContext::initDefaultExtensions(){
    m_enabledExtensions.instance.clear();
    m_enabledExtensions.layers.clear();
    m_enabledExtensions.device.clear();

    m_optionalExtensions.instance.clear();
    m_optionalExtensions.layers.clear();
    m_optionalExtensions.device.clear();

    m_rayTracingExtensions.clear();

    for(const auto name : s_EnabledInstanceExts)
        m_enabledExtensions.instance.emplace(GraphicsString(name, m_arena));
    for(const auto& e : s_EnabledDeviceExts)
        m_enabledExtensions.device.emplace(GraphicsString(e.name, m_arena), e.feature);

    for(const auto& e : s_OptionalDeviceExts){
        if(e.feature == DeviceExtensionFeature::MeshShader && !m_deviceParams.enableNativeMeshShaders)
            continue;
        m_optionalExtensions.device.emplace(GraphicsString(e.name, m_arena), e.feature);
    }

    if(m_deviceParams.enableDebugRuntime){
        for(const auto name : s_DebugRequiredInstanceExts)
            m_enabledExtensions.instance.emplace(GraphicsString(name, m_arena));
    }

    for(const auto& e : s_RayTracingExts)
        m_rayTracingExtensions.emplace(GraphicsString(e.name, m_arena), e.feature);
}

bool BackendContext::createVulkanInstance(){
    VkResult res = VK_SUCCESS;
    m_hdr10ColorSpaceExtensionEnabled = false;

    {
        res = volkInitialize();
        if(res != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to initialize volk. {}"), ResultToString(res));
            return false;
        }
    }

#ifdef NWB_PLATFORM_WINDOWS
    if(!m_deviceParams.headlessDevice){
        m_enabledExtensions.instance.emplace(VK_KHR_SURFACE_EXTENSION_NAME, m_arena);
        m_enabledExtensions.instance.emplace(VK_KHR_WIN32_SURFACE_EXTENSION_NAME, m_arena);
        if(m_deviceParams.enableHDR10Output)
            m_optionalExtensions.instance.emplace(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME, m_arena);
    }
#elif defined(NWB_PLATFORM_LINUX)
    if(!m_deviceParams.headlessDevice){
        m_enabledExtensions.instance.emplace(VK_KHR_SURFACE_EXTENSION_NAME, m_arena);
        if(m_deviceParams.enableHDR10Output)
            m_optionalExtensions.instance.emplace(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME, m_arena);

        Common::LinuxFrame frame;
        frame.frameParam() = m_platformFrameParam;

        switch(frame.backend()){
        case Common::LinuxFrameBackend::Enum::X11:
            m_enabledExtensions.instance.emplace(VK_KHR_XLIB_SURFACE_EXTENSION_NAME, m_arena);
            break;
#if defined(NWB_WITH_WAYLAND)
        case Common::LinuxFrameBackend::Enum::Wayland:
            m_enabledExtensions.instance.emplace(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME, m_arena);
            break;
#endif
        case Common::LinuxFrameBackend::Enum::None:
        default:
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot create a Linux surface without a valid native window backend."));
            return false;
        }
    }
#endif

    for(const auto& name : m_deviceParams.requiredBackendInstanceExtensions)
        m_enabledExtensions.instance.insert(name);
    for(const auto& name : m_deviceParams.optionalBackendInstanceExtensions)
        m_optionalExtensions.instance.insert(name);

    for(const auto& name : m_deviceParams.requiredBackendLayers)
        m_enabledExtensions.layers.insert(name);
    for(const auto& name : m_deviceParams.optionalBackendLayers)
        m_optionalExtensions.layers.insert(name);

    decltype(m_enabledExtensions.instance) requiredExtensions(m_enabledExtensions.instance);

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_InstanceCreateArena, s_DeviceSetupScratchArenaBytes);

    uint32_t extensionCount = 0;
    res = vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate instance extension count. {}"), ResultToString(res));
        return false;
    }
    Vector<VkExtensionProperties, Alloc::ScratchArena> availableExtensions(extensionCount, scratchArena);
    res = vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate instance extensions. {}"), ResultToString(res));
        return false;
    }

    for(const auto& ext : availableExtensions){
        GraphicsString name(ext.extensionName, m_arena);
        const bool enableOptionalExtension = m_optionalExtensions.instance.find(name) != m_optionalExtensions.instance.end();
        requiredExtensions.erase(name);
        if(enableOptionalExtension)
            m_enabledExtensions.instance.insert(Move(name));
    }

    if(!requiredExtensions.empty()){
        auto ss = VulkanDetail::MakeScratchStringStream(scratchArena);
        ss << "Cannot create a Vulkan instance because the following required extension(s) are not supported:";
        for(const auto& ext : requiredExtensions)
            ss << "\n  - " << ext;
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: {}"), StringConvert(ss.str()));
        return false;
    }

    {
        auto ss = VulkanDetail::MakeScratchStringStream(scratchArena);
        ss << "Vulkan: Enabled instance extensions:";
        for(const auto& ext : m_enabledExtensions.instance)
            ss << "\n    " << ext;
        NWB_LOGGER_INFO(StringConvert(ss.str()));
    }

    decltype(m_enabledExtensions.layers) requiredLayers(m_enabledExtensions.layers);

    uint32_t layerCount = 0;
    res = vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate layer count. {}"), ResultToString(res));
        return false;
    }
    Vector<VkLayerProperties, Alloc::ScratchArena> availableLayers(layerCount, scratchArena);
    res = vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate layers. {}"), ResultToString(res));
        return false;
    }

    for(const auto& layer : availableLayers){
        GraphicsString name(layer.layerName, m_arena);
        const bool enableOptionalLayer = m_optionalExtensions.layers.find(name) != m_optionalExtensions.layers.end();
        requiredLayers.erase(name);
        if(enableOptionalLayer)
            m_enabledExtensions.layers.insert(Move(name));
    }

    if(!requiredLayers.empty()){
        auto ss = VulkanDetail::MakeScratchStringStream(scratchArena);
        ss << "Cannot create a Vulkan instance because the following required layer(s) are not supported:";
        for(const auto& ext : requiredLayers)
            ss << "\n  - " << ext;
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: {}"), StringConvert(ss.str()));
        return false;
    }

    // createDevice() rebuilds its device-extension sets, so retain this instance-level capability separately.
    // VK_EXT_swapchain_colorspace must have been enabled on the already-created instance before HDR10 surface
    // color spaces can be selected later.
    m_hdr10ColorSpaceExtensionEnabled = isInstanceExtensionEnabled(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME);

    {
        auto ss = VulkanDetail::MakeScratchStringStream(scratchArena);
        ss << "Vulkan: Enabled layers:";
        for(const auto& layer : m_enabledExtensions.layers)
            ss << "\n    " << layer;
        NWB_LOGGER_INFO(StringConvert(ss.str()));
    }

    auto instanceExtVec = VulkanDetail::StringSetToVector(m_enabledExtensions.instance, scratchArena);
    auto layerVec = VulkanDetail::StringSetToVector(m_enabledExtensions.layers, scratchArena);

    VkApplicationInfo applicationInfo = {};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = s_AppName.data();
    applicationInfo.applicationVersion = static_cast<uint32_t>(s_AppVersion);
    applicationInfo.pEngineName = s_AppName.data();
    applicationInfo.engineVersion = static_cast<uint32_t>(s_EngineVersion);

    res = vkEnumerateInstanceVersion(&applicationInfo.apiVersion);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate instance version. {}"), ResultToString(res));
        return false;
    }

    if(applicationInfo.apiVersion < s_MinimumVersion){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: API version {}.{}.{} is too low, at least {}.{}.{} is required.")
            , VK_API_VERSION_MAJOR(applicationInfo.apiVersion)
            , VK_API_VERSION_MINOR(applicationInfo.apiVersion)
            , VK_API_VERSION_PATCH(applicationInfo.apiVersion)
            , VK_API_VERSION_MAJOR(s_MinimumVersion)
            , VK_API_VERSION_MINOR(s_MinimumVersion)
            , VK_API_VERSION_PATCH(s_MinimumVersion)
        );
        return false;
    }

    if(VK_API_VERSION_VARIANT(applicationInfo.apiVersion) != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Unexpected API variant: {}"), VK_API_VERSION_VARIANT(applicationInfo.apiVersion));
        return false;
    }

    if(m_deviceParams.enableDebugRuntime){
        auto ss = VulkanDetail::MakeScratchStringStream(scratchArena);
        ss << "Vulkan GPU debug: instance setup"
           << "\n    Vulkan API: " << VulkanDetail::VulkanVersionToString(scratchArena, applicationInfo.apiVersion)
           << "\n    validation layer enabled: " << VulkanDetail::BoolToString(isLayerEnabled("VK_LAYER_KHRONOS_validation"))
           << "\n    debug utils extension enabled: " << VulkanDetail::BoolToString(isInstanceExtensionEnabled(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
           << "\n    enabled instance extensions: " << m_enabledExtensions.instance.size()
           << "\n    enabled layers: " << m_enabledExtensions.layers.size()
        ;
        NWB_LOGGER_ESSENTIAL_INFO(StringConvert(ss.str()));
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


void BackendContext::installDebugMessenger(){
    VkResult res = VK_SUCCESS;

    if(!vkCreateDebugUtilsMessengerEXT){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan GPU debug: vkCreateDebugUtilsMessengerEXT is unavailable; validation messages will not be routed to the logger."));
        return;
    }

    VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = VulkanDetail::VulkanDebugCallback;
    createInfo.pUserData = this;

    res = vkCreateDebugUtilsMessengerEXT(m_vulkanInstance, &createInfo, nullptr, &m_debugUtilsMessenger);
    if(res != VK_SUCCESS){
        m_debugUtilsMessenger = VK_NULL_HANDLE;
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to install debug messenger. {}"), ResultToString(res));
    }
    else
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Vulkan GPU debug: debug utils messenger installed."));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

