// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend_context.h"
#include "backend_context_capabilities.h"
#include "backend_context_detail.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////






Format::Enum GetBackBufferFormat(const DeviceCreationParameters& params)noexcept{
    if(params.headlessDevice)
        return params.swapChainFormat;

    if(params.swapChainFormat == Format::RGBA8_UNORM_SRGB)
        return Format::BGRA8_UNORM_SRGB;
    if(params.swapChainFormat == Format::RGBA8_UNORM)
        return Format::BGRA8_UNORM;
    return params.swapChainFormat;
}

ScratchString VulkanVersionToString(Alloc::ScratchArena& arena, u32 version){
    auto ss = ScratchStringStream(s_FileOpenWrite, arena);
    ss << VK_API_VERSION_MAJOR(version) << "." << VK_API_VERSION_MINOR(version) << "." << VK_API_VERSION_PATCH(version);
    if(VK_API_VERSION_VARIANT(version) != 0)
        ss << " variant " << VK_API_VERSION_VARIANT(version);
    return ss.str();
}

AStringView PhysicalDeviceTypeToString(VkPhysicalDeviceType type)noexcept{
    switch(type){
    case VK_PHYSICAL_DEVICE_TYPE_OTHER: return "other";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated GPU";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete GPU";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual GPU";
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return "CPU";
    default: return s_UnknownDebugLabel;
    }
}

AStringView SwapChainFormatToString(VkFormat format)noexcept{
    switch(format){
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return "VK_FORMAT_A2B10G10R10_UNORM_PACK32";
    case VK_FORMAT_R8G8B8A8_UNORM: return "VK_FORMAT_R8G8B8A8_UNORM";
    case VK_FORMAT_R8G8B8A8_SRGB: return "VK_FORMAT_R8G8B8A8_SRGB";
    case VK_FORMAT_B8G8R8A8_UNORM: return "VK_FORMAT_B8G8R8A8_UNORM";
    case VK_FORMAT_B8G8R8A8_SRGB: return "VK_FORMAT_B8G8R8A8_SRGB";
    default: return s_UnknownDebugLabel;
    }
}

AStringView ColorSpaceToString(VkColorSpaceKHR colorSpace)noexcept{
    switch(colorSpace){
    case VK_COLOR_SPACE_HDR10_ST2084_EXT: return "VK_COLOR_SPACE_HDR10_ST2084_EXT";
    case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR: return "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR";
    default: return s_UnknownDebugLabel;
    }
}

AStringView PresentModeToString(VkPresentModeKHR mode)noexcept{
    switch(mode){
    case VK_PRESENT_MODE_IMMEDIATE_KHR: return "VK_PRESENT_MODE_IMMEDIATE_KHR";
    case VK_PRESENT_MODE_MAILBOX_KHR: return "VK_PRESENT_MODE_MAILBOX_KHR";
    case VK_PRESENT_MODE_FIFO_KHR: return "VK_PRESENT_MODE_FIFO_KHR";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "VK_PRESENT_MODE_FIFO_RELAXED_KHR";
    default: return s_UnknownDebugLabel;
    }
}

void SetHdr10Metadata(const VolkDeviceTable& deviceDispatch, const VkDevice device, const VkSwapchainKHR swapChain){
    if(!deviceDispatch.vkSetHdrMetadataEXT || !device || !swapChain)
        return;

    VkHdrMetadataEXT metadata = {};
    metadata.sType = VK_STRUCTURE_TYPE_HDR_METADATA_EXT;
    // Rec.2020 primaries and D65 white point.
    // These values describe the HDR10 presentation contract: PQ-encoded content mastered for a 1,000-nit peak.
    metadata.displayPrimaryRed = s_Hdr10DisplayPrimaryRed;
    metadata.displayPrimaryGreen = s_Hdr10DisplayPrimaryGreen;
    metadata.displayPrimaryBlue = s_Hdr10DisplayPrimaryBlue;
    metadata.whitePoint = s_Hdr10WhitePointD65;
    metadata.maxLuminance = s_Hdr10MasteringPeakLuminance;
    metadata.minLuminance = s_Hdr10MinimumLuminance;
    metadata.maxContentLightLevel = s_Hdr10MasteringPeakLuminance;
    metadata.maxFrameAverageLightLevel = s_Hdr10MaximumFrameAverageLightLevel;
    deviceDispatch.vkSetHdrMetadataEXT(device, s_Hdr10MetadataSwapChainCount, &swapChain, &metadata);
}

u64 GetDeviceLocalMemoryBytes(const VolkInstanceTable& instanceDispatch, VkPhysicalDevice physicalDevice){
    VkPhysicalDeviceMemoryProperties memoryProperties;
    instanceDispatch.vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

    u64 bytes = 0;
    for(uint32_t heapIndex = 0; heapIndex < memoryProperties.memoryHeapCount; ++heapIndex){
        const VkMemoryHeap& heap = memoryProperties.memoryHeaps[heapIndex];
        if(heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            bytes += static_cast<u64>(heap.size);
    }

    return bytes;
}

void PopulateAdapterInfo(const VolkInstanceTable& instanceDispatch, VkPhysicalDevice physicalDevice, AdapterInfo& outAdapter){
    VkPhysicalDeviceProperties2 properties2 = {};
    properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    VkPhysicalDeviceIDProperties idProperties = {};
    idProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    properties2.pNext = &idProperties;
    instanceDispatch.vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);

    const auto& properties = properties2.properties;
    outAdapter.name = properties.deviceName;
    outAdapter.vendorID = properties.vendorID;
    outAdapter.deviceID = properties.deviceID;
    outAdapter.dedicatedVideoMemory = GetDeviceLocalMemoryBytes(instanceDispatch, physicalDevice);

    NWB_MEMCPY(outAdapter.uuid.data(), outAdapter.uuid.size(), idProperties.deviceUUID, outAdapter.uuid.size());
    outAdapter.hasUUID = true;
    outAdapter.luid = {};
    outAdapter.hasLUID = false;
    if(idProperties.deviceLUIDValid){
        NWB_MEMCPY(outAdapter.luid.data(), outAdapter.luid.size(), idProperties.deviceLUID, outAdapter.luid.size());
        outAdapter.hasLUID = true;
    }
}







AStringView DebugUtilsSeverityToString(const VkDebugUtilsMessageSeverityFlagBitsEXT severity)noexcept{
    switch(severity){
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: return "verbose";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: return "info";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: return "warning";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: return "error";
    default: return s_UnknownDebugLabel;
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData){
    const i32 messageId = callbackData ? callbackData->messageIdNumber : 0;
    const auto* backend = static_cast<const BackendContext*>(userData);
    if(backend && backend->isValidationMessageIdIgnored(messageId))
        return VK_FALSE;

    const AStringView messageIdName = callbackData && callbackData->pMessageIdName ? callbackData->pMessageIdName : "";
    const AStringView message = callbackData && callbackData->pMessage ? callbackData->pMessage : "";
    NWB_LOGGER_WARNING(NWB_TEXT("Vulkan debug: [severity={} types=0x{:x} id={} name='{}'] {}"), ConvertString(DebugUtilsSeverityToString(severity)), static_cast<u32>(types), messageId, ConvertString(messageIdName), ConvertString(message));

    return VK_FALSE;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


BackendContext::BackendContext(const DeviceCreationParameters& params, SwapChainRuntimeState& swapChainState, GraphicsAllocator& allocator, CpuTaskScheduler& cpuScheduler)
    : m_deviceParams(params)
    , m_swapChainState(swapChainState)
    , m_allocator(allocator)
    , m_cpuScheduler(cpuScheduler)
    , m_arena(m_allocator.getObjectArena())
    , m_enabledExtensions(m_arena)
    , m_optionalExtensions(m_arena)
    , m_rayTracingExtensions(0, ExtensionNameHasher(), ExtensionNameEqualTo(), m_arena)
    , m_rendererString(m_arena)
    , m_swapChainImages(m_arena)
    , m_nativeQueues(m_arena)
    , m_sameClassQueues(m_arena)
    , m_acquireSyncSlots(m_arena)
    , m_presentSemaphores(m_arena)
    , m_framesInFlight(Deque<EventQueryHandle, Alloc::GlobalArena>(m_arena))
    , m_queryPool(m_arena)
    , m_swapChainLifecycle(*this)
    , m_framePresentation(*this)
{
    initDefaultExtensions();
}

b8 BackendContext::isValidationMessageIdIgnored(i32 messageId)const noexcept{
    for(const auto& ignored : m_deviceParams.ignoredValidationMessageIds){
        if(ignored == messageId)
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

