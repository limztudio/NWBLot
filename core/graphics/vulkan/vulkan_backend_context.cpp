// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend_context.h"

#include <logger/client/logger.h>

#include <sstream>

#ifdef NWB_PLATFORM_WINDOWS
#include <windows.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Set>
static Vector<const char*, Alloc::ScratchAllocator<const char*>> StringSetToVector(const Set& set, Alloc::ScratchArena<>& arena){
    Alloc::ScratchAllocator<const char*> alloc(arena);
    Vector<const char*, Alloc::ScratchAllocator<const char*>> ret(alloc);
    ret.resize(set.size());
    usize index = 0u;
    for(const auto& s : set){
        ret[index] = s.c_str();
        ++index;
    }
    return ret;
}

template<typename Map>
static Vector<const char*, Alloc::ScratchAllocator<const char*>> StringMapKeysToVector(const Map& map, Alloc::ScratchArena<>& arena){
    Alloc::ScratchAllocator<const char*> alloc(arena);
    Vector<const char*, Alloc::ScratchAllocator<const char*>> ret(alloc);
    ret.resize(map.size());
    usize index = 0u;
    for(const auto& [key, val] : map){
        ret[index] = key.c_str();
        ++index;
    }
    return ret;
}

template<typename T>
static T MakeVkFeatureStruct(VkStructureType sType){
    T feature = {};
    feature.sType = sType;
    feature.pNext = nullptr;
    return feature;
}

struct OptionalDeviceFeatureSet{
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructure = MakeVkFeatureStruct<VkPhysicalDeviceAccelerationStructureFeaturesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR);
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipeline = MakeVkFeatureStruct<VkPhysicalDeviceRayTracingPipelineFeaturesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR);
    VkPhysicalDeviceRayQueryFeaturesKHR rayQuery = MakeVkFeatureStruct<VkPhysicalDeviceRayQueryFeaturesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR);
    VkPhysicalDeviceOpacityMicromapFeaturesEXT opacityMicromap = MakeVkFeatureStruct<VkPhysicalDeviceOpacityMicromapFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_FEATURES_EXT);
    VkPhysicalDeviceClusterAccelerationStructureFeaturesNV clusterAccelerationStructure = MakeVkFeatureStruct<VkPhysicalDeviceClusterAccelerationStructureFeaturesNV>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_FEATURES_NV);
    VkPhysicalDeviceRayTracingInvocationReorderFeaturesNV rayTracingInvocationReorder = MakeVkFeatureStruct<VkPhysicalDeviceRayTracingInvocationReorderFeaturesNV>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_FEATURES_NV);
    VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV rayTracingLinearSweptSpheres = MakeVkFeatureStruct<VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_LINEAR_SWEPT_SPHERES_FEATURES_NV);
    VkPhysicalDeviceMeshShaderFeaturesEXT meshShader = MakeVkFeatureStruct<VkPhysicalDeviceMeshShaderFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT);
    VkPhysicalDeviceFragmentShadingRateFeaturesKHR fragmentShadingRate = MakeVkFeatureStruct<VkPhysicalDeviceFragmentShadingRateFeaturesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR);
    VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT mutableDescriptorType = MakeVkFeatureStruct<VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT);
    VkPhysicalDeviceDescriptorHeapFeaturesEXT descriptorHeap = MakeVkFeatureStruct<VkPhysicalDeviceDescriptorHeapFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_FEATURES_EXT);
};

static OptionalDeviceFeatureSet MakeRequestedOptionalDeviceFeatures(){
    OptionalDeviceFeatureSet features;

    features.accelerationStructure.accelerationStructure = VK_TRUE;

    features.rayTracingPipeline.rayTracingPipeline = VK_TRUE;
    features.rayTracingPipeline.rayTraversalPrimitiveCulling = VK_TRUE;

    features.rayQuery.rayQuery = VK_TRUE;

    features.opacityMicromap.micromap = VK_TRUE;

    features.clusterAccelerationStructure.clusterAccelerationStructure = VK_TRUE;

    features.rayTracingInvocationReorder.rayTracingInvocationReorder = VK_TRUE;

    features.rayTracingLinearSweptSpheres.spheres = VK_FALSE;
    features.rayTracingLinearSweptSpheres.linearSweptSpheres = VK_FALSE;

    features.meshShader.meshShader = VK_TRUE;

    features.fragmentShadingRate.pipelineFragmentShadingRate = VK_TRUE;
    features.fragmentShadingRate.primitiveFragmentShadingRate = VK_TRUE;
    features.fragmentShadingRate.attachmentFragmentShadingRate = VK_TRUE;

    features.mutableDescriptorType.mutableDescriptorType = VK_TRUE;

    features.descriptorHeap.descriptorHeap = VK_TRUE;

    return features;
}

static void* GetOptionalDeviceFeatureStruct(OptionalDeviceFeatureSet& features, DeviceExtensionFeature::Enum feature){
    switch(feature){
    case DeviceExtensionFeature::AccelerationStructure: return &features.accelerationStructure;
    case DeviceExtensionFeature::RayTracingPipeline: return &features.rayTracingPipeline;
    case DeviceExtensionFeature::RayQuery: return &features.rayQuery;
    case DeviceExtensionFeature::OpacityMicromap: return &features.opacityMicromap;
    case DeviceExtensionFeature::ClusterAccelerationStructure: return &features.clusterAccelerationStructure;
    case DeviceExtensionFeature::RayTracingInvocationReorder: return &features.rayTracingInvocationReorder;
    case DeviceExtensionFeature::RayTracingLinearSweptSpheres: return &features.rayTracingLinearSweptSpheres;
    case DeviceExtensionFeature::MeshShader: return &features.meshShader;
    case DeviceExtensionFeature::FragmentShadingRate: return &features.fragmentShadingRate;
    case DeviceExtensionFeature::MutableDescriptorType: return &features.mutableDescriptorType;
    case DeviceExtensionFeature::DescriptorHeap: return &features.descriptorHeap;
    case DeviceExtensionFeature::None:
    case DeviceExtensionFeature::Count:
    default:
        return nullptr;
    }
}

static Format::Enum GetBackBufferFormat(const DeviceCreationParameters& params){
    if(params.headlessDevice)
        return params.swapChainFormat;

    if(params.swapChainFormat == Format::RGBA8_UNORM_SRGB)
        return Format::BGRA8_UNORM_SRGB;
    if(params.swapChainFormat == Format::RGBA8_UNORM)
        return Format::BGRA8_UNORM;
    return params.swapChainFormat;
}

static bool SupportsRequestedValue(VkBool32 requested, VkBool32 supported){
    return requested != VK_TRUE || supported == VK_TRUE;
}

static const char* BoolToString(bool value){
    return value ? "yes" : "no";
}

static AString VulkanVersionToString(u32 version){
    AStringStream ss;
    ss << VK_API_VERSION_MAJOR(version)
       << "."
       << VK_API_VERSION_MINOR(version)
       << "."
       << VK_API_VERSION_PATCH(version)
    ;
    if(VK_API_VERSION_VARIANT(version) != 0)
        ss << " variant " << VK_API_VERSION_VARIANT(version);
    return ss.str();
}

static const char* PhysicalDeviceTypeToString(VkPhysicalDeviceType type){
    switch(type){
    case VK_PHYSICAL_DEVICE_TYPE_OTHER: return "other";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated GPU";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete GPU";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual GPU";
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return "CPU";
    default: return "unknown";
    }
}

static const char* SwapChainFormatToString(VkFormat format){
    switch(format){
    case VK_FORMAT_R8G8B8A8_UNORM: return "VK_FORMAT_R8G8B8A8_UNORM";
    case VK_FORMAT_R8G8B8A8_SRGB: return "VK_FORMAT_R8G8B8A8_SRGB";
    case VK_FORMAT_B8G8R8A8_UNORM: return "VK_FORMAT_B8G8R8A8_UNORM";
    case VK_FORMAT_B8G8R8A8_SRGB: return "VK_FORMAT_B8G8R8A8_SRGB";
    default: return "unknown";
    }
}

static const char* ColorSpaceToString(VkColorSpaceKHR colorSpace){
    switch(colorSpace){
    case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR: return "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR";
    default: return "unknown";
    }
}

static const char* PresentModeToString(VkPresentModeKHR mode){
    switch(mode){
    case VK_PRESENT_MODE_IMMEDIATE_KHR: return "VK_PRESENT_MODE_IMMEDIATE_KHR";
    case VK_PRESENT_MODE_MAILBOX_KHR: return "VK_PRESENT_MODE_MAILBOX_KHR";
    case VK_PRESENT_MODE_FIFO_KHR: return "VK_PRESENT_MODE_FIFO_KHR";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "VK_PRESENT_MODE_FIFO_RELAXED_KHR";
    default: return "unknown";
    }
}

static u64 GetDeviceLocalMemoryBytes(VkPhysicalDevice physicalDevice){
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

    u64 bytes = 0;
    for(uint32_t heapIndex = 0; heapIndex < memoryProperties.memoryHeapCount; ++heapIndex){
        const VkMemoryHeap& heap = memoryProperties.memoryHeaps[heapIndex];
        if(heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            bytes += static_cast<u64>(heap.size);
    }

    return bytes;
}

static u64 BytesToMiB(u64 bytes){
    return bytes / (1024ull * 1024ull);
}

static bool SupportsRequestedOptionalDeviceFeature(const OptionalDeviceFeatureSet& requested, const OptionalDeviceFeatureSet& supported, DeviceExtensionFeature::Enum feature){
    switch(feature){
    case DeviceExtensionFeature::AccelerationStructure:
        return SupportsRequestedValue(requested.accelerationStructure.accelerationStructure, supported.accelerationStructure.accelerationStructure);
    case DeviceExtensionFeature::RayTracingPipeline:
        return
            SupportsRequestedValue(requested.rayTracingPipeline.rayTracingPipeline, supported.rayTracingPipeline.rayTracingPipeline)
            && SupportsRequestedValue(requested.rayTracingPipeline.rayTracingPipelineShaderGroupHandleCaptureReplay, supported.rayTracingPipeline.rayTracingPipelineShaderGroupHandleCaptureReplay)
            && SupportsRequestedValue(requested.rayTracingPipeline.rayTracingPipelineShaderGroupHandleCaptureReplayMixed, supported.rayTracingPipeline.rayTracingPipelineShaderGroupHandleCaptureReplayMixed)
            && SupportsRequestedValue(requested.rayTracingPipeline.rayTracingPipelineTraceRaysIndirect, supported.rayTracingPipeline.rayTracingPipelineTraceRaysIndirect)
            && SupportsRequestedValue(requested.rayTracingPipeline.rayTraversalPrimitiveCulling, supported.rayTracingPipeline.rayTraversalPrimitiveCulling)
        ;
    case DeviceExtensionFeature::RayQuery:
        return SupportsRequestedValue(requested.rayQuery.rayQuery, supported.rayQuery.rayQuery);
    case DeviceExtensionFeature::OpacityMicromap:
        return SupportsRequestedValue(requested.opacityMicromap.micromap, supported.opacityMicromap.micromap);
    case DeviceExtensionFeature::ClusterAccelerationStructure:
        return SupportsRequestedValue(requested.clusterAccelerationStructure.clusterAccelerationStructure, supported.clusterAccelerationStructure.clusterAccelerationStructure);
    case DeviceExtensionFeature::RayTracingInvocationReorder:
        return SupportsRequestedValue(requested.rayTracingInvocationReorder.rayTracingInvocationReorder, supported.rayTracingInvocationReorder.rayTracingInvocationReorder);
    case DeviceExtensionFeature::RayTracingLinearSweptSpheres:
        return
            supported.rayTracingLinearSweptSpheres.spheres == VK_TRUE
            || supported.rayTracingLinearSweptSpheres.linearSweptSpheres == VK_TRUE
        ;
    case DeviceExtensionFeature::MeshShader:
        return
            SupportsRequestedValue(requested.meshShader.taskShader, supported.meshShader.taskShader)
            && SupportsRequestedValue(requested.meshShader.meshShader, supported.meshShader.meshShader)
        ;
    case DeviceExtensionFeature::FragmentShadingRate:
        return
            SupportsRequestedValue(requested.fragmentShadingRate.pipelineFragmentShadingRate, supported.fragmentShadingRate.pipelineFragmentShadingRate)
            && SupportsRequestedValue(requested.fragmentShadingRate.primitiveFragmentShadingRate, supported.fragmentShadingRate.primitiveFragmentShadingRate)
            && SupportsRequestedValue(requested.fragmentShadingRate.attachmentFragmentShadingRate, supported.fragmentShadingRate.attachmentFragmentShadingRate)
        ;
    case DeviceExtensionFeature::MutableDescriptorType:
        return SupportsRequestedValue(requested.mutableDescriptorType.mutableDescriptorType, supported.mutableDescriptorType.mutableDescriptorType);
    case DeviceExtensionFeature::DescriptorHeap:
        return SupportsRequestedValue(requested.descriptorHeap.descriptorHeap, supported.descriptorHeap.descriptorHeap);
    case DeviceExtensionFeature::None:
    case DeviceExtensionFeature::Count:
    default:
        return true;
    }
}

static void FinalizeOptionalDeviceFeatureEnablement(OptionalDeviceFeatureSet& enabled, const OptionalDeviceFeatureSet& supported){
    enabled.descriptorHeap.descriptorHeapCaptureReplay = supported.descriptorHeap.descriptorHeapCaptureReplay;
    enabled.meshShader.taskShader = supported.meshShader.taskShader;
    enabled.rayTracingLinearSweptSpheres.spheres = supported.rayTracingLinearSweptSpheres.spheres;
    enabled.rayTracingLinearSweptSpheres.linearSweptSpheres = supported.rayTracingLinearSweptSpheres.linearSweptSpheres;
}

static void AppendFeatureStruct(void*& pNext, void* feature){
    reinterpret_cast<VkBaseOutStructure*>(feature)->pNext = reinterpret_cast<VkBaseOutStructure*>(pNext);
    pNext = feature;
}

static void AppendOptionalDeviceFeature(void*& pNext, OptionalDeviceFeatureSet& features, DeviceExtensionFeature::Enum feature, bool* appended){
    if(feature == DeviceExtensionFeature::None || feature == DeviceExtensionFeature::Count)
        return;

    const usize featureIndex = static_cast<usize>(feature);
    if(appended[featureIndex])
        return;

    if(void* featureStruct = GetOptionalDeviceFeatureStruct(features, feature)){
        AppendFeatureStruct(pNext, featureStruct);
        appended[featureIndex] = true;
    }
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
){
    static_cast<void>(flags);
    static_cast<void>(objType);
    static_cast<void>(obj);

    const auto* backend = static_cast<const BackendContext*>(userData);
    if(backend && backend->isValidationMessageLocationIgnored(static_cast<usize>(location)))
        return VK_FALSE;

    NWB_LOGGER_WARNING(NWB_TEXT("Vulkan validation: [location=0x{:x} code={} layer='{}'] {}"), location, code, StringConvert(layerPrefix), StringConvert(msg));

    return VK_FALSE;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


BackendContext::BackendContext(
    const DeviceCreationParameters& params,
    SwapChainRuntimeState& swapChainState,
    GraphicsAllocator& allocator,
    Alloc::ThreadPool& threadPool
)
    : m_deviceParams(params)
    , m_swapChainState(swapChainState)
    , m_allocator(allocator)
    , m_threadPool(threadPool)
    , m_arena(m_allocator.getObjectArena())
    , m_enabledExtensions(m_arena)
    , m_optionalExtensions(m_arena)
    , m_rayTracingExtensions(0, Hasher<AString>(), EqualTo<AString>(), Alloc::CustomAllocator<Pair<const AString, DeviceExtensionFeature::Enum>>(m_arena))
    , m_swapChainImages(Alloc::CustomAllocator<SwapChainImage>(m_arena))
    , m_acquireSemaphores(Alloc::CustomAllocator<VkSemaphore>(m_arena))
    , m_presentSemaphores(Alloc::CustomAllocator<VkSemaphore>(m_arena))
    , m_framesInFlight(Deque<EventQueryHandle, Alloc::CustomAllocator<EventQueryHandle>>(Alloc::CustomAllocator<EventQueryHandle>(m_arena)))
    , m_queryPool(Alloc::CustomAllocator<EventQueryHandle>(m_arena))
{
    initDefaultExtensions();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void* BackendContext::queryInterface(GraphicsBackendInterfaceID interfaceID){
    if(interfaceID == s_BackendQueriesInterfaceID)
        return static_cast<IBackendQueries*>(this);

    return IGraphicsBackend::queryInterface(interfaceID);
}

const void* BackendContext::queryInterface(GraphicsBackendInterfaceID interfaceID)const{
    if(interfaceID == s_BackendQueriesInterfaceID)
        return static_cast<const IBackendQueries*>(this);

    return IGraphicsBackend::queryInterface(interfaceID);
}

bool BackendContext::isValidationMessageLocationIgnored(usize location)const{
    for(const auto& ignored : m_deviceParams.ignoredVulkanValidationMessageLocations){
        if(ignored == location)
            return true;
    }
    return false;
}

void BackendContext::getEnabledInstanceExtensions(Vector<AString>& extensions)const{
    extensions.clear();
    extensions.resize(m_enabledExtensions.instance.size());
    usize index = 0u;
    for(const auto& ext : m_enabledExtensions.instance){
        extensions[index] = ext;
        ++index;
    }
}

void BackendContext::getEnabledDeviceExtensions(Vector<AString>& extensions)const{
    extensions.clear();
    extensions.resize(m_enabledExtensions.device.size());
    usize index = 0u;
    for(const auto& [name, _] : m_enabledExtensions.device){
        extensions[index] = name;
        ++index;
    }
}

void BackendContext::getEnabledLayers(Vector<AString>& layers)const{
    layers.clear();
    layers.resize(m_enabledExtensions.layers.size());
    usize index = 0u;
    for(const auto& ext : m_enabledExtensions.layers){
        layers[index] = ext;
        ++index;
    }
}

ITexture* BackendContext::getCurrentBackBuffer(){
    if(m_swapChainIndex < m_swapChainImages.size())
        return m_swapChainImages[m_swapChainIndex].rhiHandle.get();
    return nullptr;
}

ITexture* BackendContext::getBackBuffer(u32 index){
    if(index < m_swapChainImages.size())
        return m_swapChainImages[index].rhiHandle.get();
    return nullptr;
}

void BackendContext::clearSemaphores(SemaphoreVector& semaphores){
    if(m_vulkanDevice){
        for(auto& semaphore : semaphores){
            if(semaphore)
                vkDestroySemaphore(m_vulkanDevice, semaphore, nullptr);
            semaphore = VK_NULL_HANDLE;
        }
    }
    semaphores.clear();
}

bool BackendContext::recreateSemaphores(SemaphoreVector& semaphores, const usize count, const AStringView operationName){
    VkResult res = VK_SUCCESS;

    clearSemaphores(semaphores);
    semaphores.resize(count, VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semInfo = {};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    for(usize i = 0; i < count; ++i){
        VkSemaphore sem = VK_NULL_HANDLE;
        res = vkCreateSemaphore(m_vulkanDevice, &semInfo, nullptr, &sem);
        if(res != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}. {}"), StringConvert(operationName), ResultToString(res));
            clearSemaphores(semaphores);
            return false;
        }
        semaphores[i] = sem;
    }

    return true;
}

void BackendContext::resizeSwapChain(){
    if(m_vulkanDevice){
        destroySwapChain();
        if(!createVulkanSwapChain()){
            clearSemaphores(m_presentSemaphores);
            clearSemaphores(m_acquireSemaphores);
            return;
        }

        const usize requiredPresentSemaphores = m_swapChainImages.size();
        if(m_presentSemaphores.size() != requiredPresentSemaphores){
            if(!recreateSemaphores(m_presentSemaphores, requiredPresentSemaphores, "recreate present semaphores during resize")){
                clearSemaphores(m_acquireSemaphores);
                destroySwapChain();
                return;
            }
        }

        const usize requiredAcquireSemaphores = Max<usize>(m_maxFramesInFlight, m_swapChainImages.size());
        if(m_acquireSemaphores.size() != requiredAcquireSemaphores){
            if(!recreateSemaphores(m_acquireSemaphores, requiredAcquireSemaphores, "recreate acquire semaphores during resize")){
                clearSemaphores(m_presentSemaphores);
                destroySwapChain();
                return;
            }
        }

        if(m_presentSemaphores.empty() || m_acquireSemaphores.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Swap chain resize left semaphore pools empty; destroying swap chain."));
            clearSemaphores(m_presentSemaphores);
            clearSemaphores(m_acquireSemaphores);
            destroySwapChain();
            return;
        }

        m_swapChainIndex = 0;
        m_acquireSemaphoreIndex = 0;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Instance creation


void BackendContext::initDefaultExtensions(){
    m_enabledExtensions.instance.clear();
    m_enabledExtensions.layers.clear();
    m_enabledExtensions.device.clear();

    m_optionalExtensions.instance.clear();
    m_optionalExtensions.layers.clear();
    m_optionalExtensions.device.clear();

    m_rayTracingExtensions.clear();

    for(const auto* name : s_EnabledInstanceExts)
        m_enabledExtensions.instance.insert(name);
    for(const auto& e : s_EnabledDeviceExts)
        m_enabledExtensions.device.emplace(e.name, e.feature);

    for(const auto* name : s_OptionalInstanceExts)
        m_optionalExtensions.instance.insert(name);
    for(const auto& e : s_OptionalDeviceExts)
        m_optionalExtensions.device.emplace(e.name, e.feature);

    for(const auto& e : s_RayTracingExts)
        m_rayTracingExtensions.emplace(e.name, e.feature);
}

bool BackendContext::createVulkanInstance(){
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
#elif defined(NWB_PLATFORM_LINUX)
    if(!m_deviceParams.headlessDevice){
        m_enabledExtensions.instance.insert(VK_KHR_SURFACE_EXTENSION_NAME);

        Common::LinuxFrame frame;
        frame.frameParam() = m_platformFrameParam;

        switch(frame.backend()){
        case Common::LinuxFrameBackend::Enum::X11:
            m_enabledExtensions.instance.insert(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
            break;
#if defined(NWB_WITH_WAYLAND)
        case Common::LinuxFrameBackend::Enum::Wayland:
            m_enabledExtensions.instance.insert(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
            break;
#endif
        case Common::LinuxFrameBackend::Enum::None:
        default:
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot create a Linux surface without a valid native window backend."));
            return false;
        }
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
    res = vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate instance extension count. {}"), ResultToString(res));
        return false;
    }
    Vector<VkExtensionProperties, Alloc::ScratchAllocator<VkExtensionProperties>> availableExtensions(extensionCount, Alloc::ScratchAllocator<VkExtensionProperties>(scratchArena));
    res = vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate instance extensions. {}"), ResultToString(res));
        return false;
    }

    for(const auto& ext : availableExtensions){
        AString name = ext.extensionName;
        const bool enableOptionalExtension =
            m_optionalExtensions.instance.find(name) != m_optionalExtensions.instance.end()
        ;
        requiredExtensions.erase(name);
        if(enableOptionalExtension)
            m_enabledExtensions.instance.insert(Move(name));
    }

    if(!requiredExtensions.empty()){
        AStringStream ss;
        ss << "Cannot create a Vulkan instance because the following required extension(s) are not supported:";
        for(const auto& ext : requiredExtensions)
            ss << "\n  - " << ext;
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
    res = vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate layer count. {}"), ResultToString(res));
        return false;
    }
    Vector<VkLayerProperties, Alloc::ScratchAllocator<VkLayerProperties>> availableLayers(layerCount, Alloc::ScratchAllocator<VkLayerProperties>(scratchArena));
    res = vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate layers. {}"), ResultToString(res));
        return false;
    }

    for(const auto& layer : availableLayers){
        AString name = layer.layerName;
        const bool enableOptionalLayer = m_optionalExtensions.layers.find(name) != m_optionalExtensions.layers.end();
        requiredLayers.erase(name);
        if(enableOptionalLayer)
            m_enabledExtensions.layers.insert(Move(name));
    }

    if(!requiredLayers.empty()){
        AStringStream ss;
        ss << "Cannot create a Vulkan instance because the following required layer(s) are not supported:";
        for(const auto& ext : requiredLayers)
            ss << "\n  - " << ext;
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

    auto instanceExtVec = VulkanDetail::StringSetToVector(m_enabledExtensions.instance, scratchArena);
    auto layerVec = VulkanDetail::StringSetToVector(m_enabledExtensions.layers, scratchArena);

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
        AStringStream ss;
        ss << "Vulkan GPU debug: instance setup"
           << "\n    Vulkan API: " << VulkanDetail::VulkanVersionToString(applicationInfo.apiVersion)
           << "\n    validation layer enabled: " << VulkanDetail::BoolToString(isLayerEnabled("VK_LAYER_KHRONOS_validation"))
           << "\n    debug report extension enabled: " << VulkanDetail::BoolToString(isInstanceExtensionEnabled(VK_EXT_DEBUG_REPORT_EXTENSION_NAME))
           << "\n    debug utils extension enabled: " << VulkanDetail::BoolToString(isInstanceExtensionEnabled(VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
           << "\n    enabled instance extensions: " << m_enabledExtensions.instance.size()
           << "\n    enabled layers: " << m_enabledExtensions.layers.size()
        ;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("{}"), StringConvert(ss.str()));
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


void BackendContext::installDebugCallback(){
    VkResult res = VK_SUCCESS;

    auto* createFunc = reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(vkGetInstanceProcAddr(m_vulkanInstance, "vkCreateDebugReportCallbackEXT"));
    if(!createFunc){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan GPU debug: vkCreateDebugReportCallbackEXT is unavailable; validation messages will not be routed to the logger."));
        return;
    }

    VkDebugReportCallbackCreateInfoEXT createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
    createInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT | VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
    createInfo.pfnCallback = VulkanDetail::VulkanDebugCallback;
    createInfo.pUserData = this;

    res = createFunc(m_vulkanInstance, &createInfo, nullptr, &m_debugReportCallback);
    if(res != VK_SUCCESS){
        m_debugReportCallback = VK_NULL_HANDLE;
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to install debug callback. {}"), ResultToString(res));
    }
    else
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Vulkan GPU debug: debug report callback installed."));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Physical device selection


bool BackendContext::findQueueFamilies(VkPhysicalDevice physicalDevice){
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);

    Alloc::ScratchArena<> scratchArena;

    Vector<VkQueueFamilyProperties, Alloc::ScratchAllocator<VkQueueFamilyProperties>> props(queueFamilyCount, Alloc::ScratchAllocator<VkQueueFamilyProperties>(scratchArena));
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, props.data());

    m_graphicsQueueFamily = -1;
    m_computeQueueFamily = -1;
    m_transferQueueFamily = -1;
    m_presentQueueFamily = -1;

    const bool requirePresentQueue = !m_deviceParams.headlessDevice;
    const bool requireComputeQueue = m_deviceParams.enableComputeQueue;
    const bool requireTransferQueue = m_deviceParams.enableCopyQueue;

    for(i32 i = 0; i < static_cast<i32>(props.size()); ++i){
        const auto& queueFamily = props[i];

        if(m_graphicsQueueFamily == -1){
            if(
                queueFamily.queueCount > 0
                && (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            )
                m_graphicsQueueFamily = i;
        }

        if(m_computeQueueFamily == -1){
            if(
                queueFamily.queueCount > 0
                && (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)
                && !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            )
                m_computeQueueFamily = i;
        }

        if(m_transferQueueFamily == -1){
            if(
                queueFamily.queueCount > 0
                && (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT)
                && !(queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)
                && !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            )
                m_transferQueueFamily = i;
        }

#ifdef NWB_PLATFORM_WINDOWS
        if(requirePresentQueue && m_presentQueueFamily == -1){
            if(queueFamily.queueCount > 0){
                VkBool32 supported = vkGetPhysicalDeviceWin32PresentationSupportKHR(physicalDevice, i);
                if(supported)
                    m_presentQueueFamily = i;
            }
        }
#elif defined(NWB_PLATFORM_LINUX)
        VkResult res = VK_SUCCESS;
        if(requirePresentQueue && m_presentQueueFamily == -1 && m_windowSurface){
            if(queueFamily.queueCount > 0){
                VkBool32 supported = VK_FALSE;
                res = vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, m_windowSurface, &supported);
                if(res == VK_SUCCESS && supported)
                    m_presentQueueFamily = i;
            }
        }
#endif

        if(
            m_graphicsQueueFamily != -1
            && (!requirePresentQueue || m_presentQueueFamily != -1)
            && (!requireComputeQueue || m_computeQueueFamily != -1)
            && (!requireTransferQueue || m_transferQueueFamily != -1)
        )
            break;
    }

    if(
        m_graphicsQueueFamily == -1
        || (m_presentQueueFamily == -1 && requirePresentQueue)
        || (m_computeQueueFamily == -1 && requireComputeQueue)
        || (m_transferQueueFamily == -1 && requireTransferQueue)
    )
        return false;

    return true;
}

bool BackendContext::pickPhysicalDevice(){
    VkFormat requestedFormat = VulkanDetail::ConvertFormat(m_swapChainState.backBufferFormat);
    if(!m_deviceParams.headlessDevice && requestedFormat == VK_FORMAT_UNDEFINED){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Requested swapchain format is unsupported"));
        return false;
    }
    VkExtent2D requestedExtent = { m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight };

    VkResult res = VK_SUCCESS;

    uint32_t deviceCount = 0;
    res = vkEnumeratePhysicalDevices(m_vulkanInstance, &deviceCount, nullptr);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate physical device count. {}"), ResultToString(res));
        return false;
    }

    Alloc::ScratchArena<> scratchArena(32768);

    Vector<VkPhysicalDevice, Alloc::ScratchAllocator<VkPhysicalDevice>> devices(deviceCount, Alloc::ScratchAllocator<VkPhysicalDevice>(scratchArena));
    res = vkEnumeratePhysicalDevices(m_vulkanInstance, &deviceCount, devices.data());
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate physical devices. {}"), ResultToString(res));
        return false;
    }

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

    struct DeviceSelection{
        VkPhysicalDevice device = VK_NULL_HANDLE;
        i32 graphicsQueueFamily = -1;
        i32 computeQueueFamily = -1;
        i32 transferQueueFamily = -1;
        i32 presentQueueFamily = -1;
    };
    auto captureCurrentSelection = [this](VkPhysicalDevice device){
        DeviceSelection selection;
        selection.device = device;
        selection.graphicsQueueFamily = m_graphicsQueueFamily;
        selection.computeQueueFamily = m_computeQueueFamily;
        selection.transferQueueFamily = m_transferQueueFamily;
        selection.presentQueueFamily = m_presentQueueFamily;
        return selection;
    };
    auto applySelection = [this](const DeviceSelection& selection){
        m_vulkanPhysicalDevice = selection.device;
        m_graphicsQueueFamily = selection.graphicsQueueFamily;
        m_computeQueueFamily = selection.computeQueueFamily;
        m_transferQueueFamily = selection.transferQueueFamily;
        m_presentQueueFamily = selection.presentQueueFamily;
    };
    DeviceSelection fallbackSelection;

    for(i32 deviceIndex = firstDevice; deviceIndex <= lastDevice; ++deviceIndex){
        VkPhysicalDevice dev = devices[deviceIndex];
        VkPhysicalDeviceProperties prop;
        vkGetPhysicalDeviceProperties(dev, &prop);

        errorStream << "\n" << prop.deviceName << ":";

        HashSet<AString, Hasher<AString>, EqualTo<AString>, Alloc::ScratchAllocator<AString>> requiredExtensions(0, Hasher<AString>(), EqualTo<AString>(), Alloc::ScratchAllocator<AString>(scratchArena));
        requiredExtensions.reserve(m_enabledExtensions.device.size());
        for(const auto& [name, _] : m_enabledExtensions.device)
            requiredExtensions.insert(name);
        uint32_t extCount = 0;
        res = vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
        if(res != VK_SUCCESS){
            errorStream << "\n  - failed to enumerate device extension count";
            continue;
        }
        Vector<VkExtensionProperties, Alloc::ScratchAllocator<VkExtensionProperties>> deviceExtensions(extCount, Alloc::ScratchAllocator<VkExtensionProperties>(scratchArena));
        res = vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, deviceExtensions.data());
        if(res != VK_SUCCESS){
            errorStream << "\n  - failed to enumerate device extensions";
            continue;
        }
        for(const auto& ext : deviceExtensions)
            requiredExtensions.erase(AString(ext.extensionName));

        bool deviceIsGood = true;

        if(!requiredExtensions.empty()){
            for(const auto& ext : requiredExtensions)
                errorStream << "\n  - missing " << ext;
            deviceIsGood = false;
        }

        VkPhysicalDeviceFeatures deviceFeatures;
        vkGetPhysicalDeviceFeatures(dev, &deviceFeatures);
        if(!deviceFeatures.samplerAnisotropy){
            errorStream << "\n  - does not support samplerAnisotropy";
            deviceIsGood = false;
        }
        if(!deviceFeatures.textureCompressionBC){
            errorStream << "\n  - does not support textureCompressionBC";
            deviceIsGood = false;
        }

        if(!findQueueFamilies(dev)){
            errorStream << "\n  - does not support the necessary queue types";
            deviceIsGood = false;
        }

        if(deviceIsGood && m_windowSurface){
            VkBool32 surfaceSupported = VK_FALSE;
            res = vkGetPhysicalDeviceSurfaceSupportKHR(dev, m_presentQueueFamily, m_windowSurface, &surfaceSupported);
            if(res != VK_SUCCESS){
                errorStream << "\n  - failed to query surface support";
                deviceIsGood = false;
            }
            else if(!surfaceSupported){
                errorStream << "\n  - does not support the window surface";
                deviceIsGood = false;
            }
            else{
                VkSurfaceCapabilitiesKHR surfaceCaps;
                res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev, m_windowSurface, &surfaceCaps);
                if(res != VK_SUCCESS){
                    errorStream << "\n  - failed to query surface capabilities";
                    continue;
                }

                uint32_t fmtCount = 0;
                res = vkGetPhysicalDeviceSurfaceFormatsKHR(dev, m_windowSurface, &fmtCount, nullptr);
                if(res != VK_SUCCESS){
                    errorStream << "\n  - failed to query surface format count";
                    continue;
                }
                Vector<VkSurfaceFormatKHR, Alloc::ScratchAllocator<VkSurfaceFormatKHR>> surfaceFmts(fmtCount, Alloc::ScratchAllocator<VkSurfaceFormatKHR>(scratchArena));
                res = vkGetPhysicalDeviceSurfaceFormatsKHR(dev, m_windowSurface, &fmtCount, surfaceFmts.data());
                if(res != VK_SUCCESS){
                    errorStream << "\n  - failed to query surface formats";
                    continue;
                }

                if(
                    surfaceCaps.minImageCount > m_deviceParams.swapChainBufferCount
                    || (surfaceCaps.maxImageCount < m_deviceParams.swapChainBufferCount && surfaceCaps.maxImageCount > 0)
                ){
                    errorStream << "\n  - cannot support the requested swap chain image count";
                    deviceIsGood = false;
                }

                if(
                    surfaceCaps.minImageExtent.width > requestedExtent.width
                    || surfaceCaps.minImageExtent.height > requestedExtent.height
                    || surfaceCaps.maxImageExtent.width < requestedExtent.width
                    || surfaceCaps.maxImageExtent.height < requestedExtent.height
                ){
                    errorStream << "\n  - cannot support the requested swap chain size";
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
                    errorStream << "\n  - does not support the requested swap chain format";
                    deviceIsGood = false;
                }
            }
        }

        if(!deviceIsGood)
            continue;

        if(prop.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU){
            applySelection(captureCurrentSelection(dev));
            return true;
        }

        if(fallbackSelection.device == VK_NULL_HANDLE)
            fallbackSelection = captureCurrentSelection(dev);
    }

    if(fallbackSelection.device != VK_NULL_HANDLE){
        applySelection(fallbackSelection);
        return true;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: {}"), StringConvert(errorStream.str()));
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Logical device creation


bool BackendContext::createVulkanDevice(){
    VkResult res = VK_SUCCESS;

    Alloc::ScratchArena<> scratchArena(32768);
    m_bufferDeviceAddressSupported = false;
    m_dynamicRenderingSupported = false;
    m_synchronization2Supported = false;
    m_meshTaskShaderSupported = false;
    m_rayTracingSpheresSupported = false;
    m_rayTracingLinearSweptSpheresSupported = false;

    uint32_t extCount = 0;
    res = vkEnumerateDeviceExtensionProperties(m_vulkanPhysicalDevice, nullptr, &extCount, nullptr);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate device extension count. {}"), ResultToString(res));
        return false;
    }
    Vector<VkExtensionProperties, Alloc::ScratchAllocator<VkExtensionProperties>> deviceExtensions(extCount, Alloc::ScratchAllocator<VkExtensionProperties>(scratchArena));
    res = vkEnumerateDeviceExtensionProperties(m_vulkanPhysicalDevice, nullptr, &extCount, deviceExtensions.data());
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate device extensions. {}"), ResultToString(res));
        return false;
    }

    for(const auto& ext : deviceExtensions){
        AString name = ext.extensionName;
        bool enableExtension = false;
        DeviceExtensionFeature::Enum enabledFeature = DeviceExtensionFeature::None;

        auto optIt = m_optionalExtensions.device.find(name);
        if(optIt != m_optionalExtensions.device.end()){
            if(name == VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME && m_deviceParams.headlessDevice)
                continue;
            enableExtension = true;
            enabledFeature = optIt.value();
        }

        if(!enableExtension && m_deviceParams.enableRayTracingExtensions){
            auto rtIt = m_rayTracingExtensions.find(name);
            if(rtIt != m_rayTracingExtensions.end()){
                enableExtension = true;
                enabledFeature = rtIt.value();
            }
        }

        if(enableExtension){
            auto [it, inserted] = m_enabledExtensions.device.emplace(Move(name), enabledFeature);
            if(!inserted && it.value() == DeviceExtensionFeature::None && enabledFeature != DeviceExtensionFeature::None)
                it.value() = enabledFeature;
        }
    }

    VkPhysicalDeviceProperties physicalDeviceProperties;
    vkGetPhysicalDeviceProperties(m_vulkanPhysicalDevice, &physicalDeviceProperties);

#ifdef NWB_UNICODE
    {
        const char* deviceName = physicalDeviceProperties.deviceName;
        const usize len = NWB_STRNLEN(deviceName, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE);
        m_rendererString = StringConvert(AStringView(deviceName, len));
    }
#else
    m_rendererString = physicalDeviceProperties.deviceName;
#endif

    const bool apiSupportsVulkan13 = physicalDeviceProperties.apiVersion >= VK_API_VERSION_1_3;
    const bool coopVecExtensionEnabled = isDeviceExtensionEnabled(VK_NV_COOPERATIVE_VECTOR_EXTENSION_NAME);
    const bool dynamicRenderingExtensionEnabled = isDeviceExtensionEnabled(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    const bool synchronization2ExtensionEnabled = isDeviceExtensionEnabled(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    const bool maintenance4ExtensionEnabled = isDeviceExtensionEnabled(VK_KHR_MAINTENANCE_4_EXTENSION_NAME);

    m_swapChainMutableFormatSupported = isDeviceExtensionEnabled(VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME);

    constexpr usize kOptionalDeviceFeatureCount = static_cast<usize>(DeviceExtensionFeature::Count);

    void* pNext = nullptr;

    auto physicalDeviceFeatures2 = VulkanDetail::MakeVkStruct<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);

    VulkanDetail::OptionalDeviceFeatureSet requestedOptionalFeatures = VulkanDetail::MakeRequestedOptionalDeviceFeatures();
    VulkanDetail::OptionalDeviceFeatureSet supportedOptionalFeatures;

    VkPhysicalDeviceVulkan11Features supportedVulkan11Features = VulkanDetail::MakeVkFeatureStruct<VkPhysicalDeviceVulkan11Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES);
    VulkanDetail::AppendFeatureStruct(pNext, &supportedVulkan11Features);

    VkPhysicalDeviceVulkan12Features supportedVulkan12Features = VulkanDetail::MakeVkFeatureStruct<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
    VulkanDetail::AppendFeatureStruct(pNext, &supportedVulkan12Features);

    VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures = VulkanDetail::MakeVkFeatureStruct<VkPhysicalDeviceBufferDeviceAddressFeatures>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES);
    VulkanDetail::AppendFeatureStruct(pNext, &bufferDeviceAddressFeatures);

    VkPhysicalDeviceSynchronization2Features synchronization2Features = VulkanDetail::MakeVkFeatureStruct<VkPhysicalDeviceSynchronization2Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES);
    if(apiSupportsVulkan13 || synchronization2ExtensionEnabled)
        VulkanDetail::AppendFeatureStruct(pNext, &synchronization2Features);

    VkPhysicalDeviceMaintenance4Features maintenance4Features = VulkanDetail::MakeVkFeatureStruct<VkPhysicalDeviceMaintenance4Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES);
    if(apiSupportsVulkan13 || maintenance4ExtensionEnabled)
        VulkanDetail::AppendFeatureStruct(pNext, &maintenance4Features);

    VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures = VulkanDetail::MakeVkFeatureStruct<VkPhysicalDeviceDynamicRenderingFeatures>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES);
    if(apiSupportsVulkan13 || dynamicRenderingExtensionEnabled)
        VulkanDetail::AppendFeatureStruct(pNext, &dynamicRenderingFeatures);

    VkPhysicalDeviceCooperativeVectorFeaturesNV cooperativeVectorFeatures = VulkanDetail::MakeVkFeatureStruct<VkPhysicalDeviceCooperativeVectorFeaturesNV>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_VECTOR_FEATURES_NV);
    if(coopVecExtensionEnabled)
        VulkanDetail::AppendFeatureStruct(pNext, &cooperativeVectorFeatures);

    bool queriedOptionalFeatures[kOptionalDeviceFeatureCount] = {};
    for(const auto& [_, feature] : m_enabledExtensions.device)
        VulkanDetail::AppendOptionalDeviceFeature(pNext, supportedOptionalFeatures, feature, queriedOptionalFeatures);

    physicalDeviceFeatures2.pNext = pNext;
    vkGetPhysicalDeviceFeatures2(m_vulkanPhysicalDevice, &physicalDeviceFeatures2);

    Vector<AString, Alloc::ScratchAllocator<AString>> unsupportedFeatureExtensions{ Alloc::ScratchAllocator<AString>(scratchArena) };
    unsupportedFeatureExtensions.reserve(m_enabledExtensions.device.size());
    for(const auto& [name, feature] : m_enabledExtensions.device){
        if(feature == DeviceExtensionFeature::None)
            continue;
        if(VulkanDetail::SupportsRequestedOptionalDeviceFeature(requestedOptionalFeatures, supportedOptionalFeatures, feature))
            continue;
        unsupportedFeatureExtensions.push_back(name);
    }

    for(const auto& name : unsupportedFeatureExtensions){
        NWB_LOGGER_INFO(NWB_TEXT("Vulkan: Disabling device extension '{}' because the selected GPU does not support its required feature set."), StringConvert(name));
        m_enabledExtensions.device.erase(name);
    }

    {
        const AString samplerFilterMinmaxExtensionName = VK_EXT_SAMPLER_FILTER_MINMAX_EXTENSION_NAME;
        const auto samplerFilterMinmaxIt = m_enabledExtensions.device.find(samplerFilterMinmaxExtensionName);
        if(samplerFilterMinmaxIt != m_enabledExtensions.device.end() && supportedVulkan12Features.samplerFilterMinmax != VK_TRUE){
            NWB_LOGGER_INFO(NWB_TEXT("Vulkan: Disabling device extension '{}' because samplerFilterMinmax is not supported."), StringConvert(samplerFilterMinmaxExtensionName));
            m_enabledExtensions.device.erase(samplerFilterMinmaxExtensionName);
        }
    }

    const bool synchronization2Enabled = apiSupportsVulkan13 || isDeviceExtensionEnabled(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    const bool maintenance4Enabled = apiSupportsVulkan13 || isDeviceExtensionEnabled(VK_KHR_MAINTENANCE_4_EXTENSION_NAME);
    const bool dynamicRenderingEnabled = apiSupportsVulkan13 || isDeviceExtensionEnabled(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);

    {
        AStringStream ss;
        ss << "Vulkan: Enabled device extensions:";
        for(const auto& [name, _] : m_enabledExtensions.device)
            ss << "\n    " << name;
        NWB_LOGGER_INFO(NWB_TEXT("{}"), StringConvert(ss.str()));
    }

    auto requireFeature = [&](const VkBool32 supported, const AStringView featureName)->bool{
        if(supported == VK_TRUE)
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Required device feature '{}' is not supported by the selected GPU."), StringConvert(featureName));
        return false;
    };

    const VkPhysicalDeviceFeatures& supportedCoreFeatures = physicalDeviceFeatures2.features;
    if(
        !requireFeature(supportedCoreFeatures.shaderImageGatherExtended, "shaderImageGatherExtended")
        || !requireFeature(supportedCoreFeatures.samplerAnisotropy, "samplerAnisotropy")
        || !requireFeature(supportedCoreFeatures.tessellationShader, "tessellationShader")
        || !requireFeature(supportedCoreFeatures.textureCompressionBC, "textureCompressionBC")
        || !requireFeature(supportedCoreFeatures.geometryShader, "geometryShader")
        || !requireFeature(supportedCoreFeatures.imageCubeArray, "imageCubeArray")
        || !requireFeature(supportedCoreFeatures.shaderInt16, "shaderInt16")
        || !requireFeature(supportedCoreFeatures.depthClamp, "depthClamp")
        || !requireFeature(supportedCoreFeatures.fillModeNonSolid, "fillModeNonSolid")
        || !requireFeature(supportedCoreFeatures.fragmentStoresAndAtomics, "fragmentStoresAndAtomics")
        || !requireFeature(supportedCoreFeatures.dualSrcBlend, "dualSrcBlend")
        || !requireFeature(supportedCoreFeatures.vertexPipelineStoresAndAtomics, "vertexPipelineStoresAndAtomics")
        || !requireFeature(supportedCoreFeatures.shaderInt64, "shaderInt64")
        || !requireFeature(supportedCoreFeatures.shaderStorageImageWriteWithoutFormat, "shaderStorageImageWriteWithoutFormat")
        || !requireFeature(supportedCoreFeatures.shaderStorageImageReadWithoutFormat, "shaderStorageImageReadWithoutFormat")
        || !requireFeature(supportedVulkan11Features.storageBuffer16BitAccess, "storageBuffer16BitAccess")
        || !requireFeature(supportedVulkan12Features.descriptorIndexing, "descriptorIndexing")
        || !requireFeature(supportedVulkan12Features.runtimeDescriptorArray, "runtimeDescriptorArray")
        || !requireFeature(supportedVulkan12Features.descriptorBindingPartiallyBound, "descriptorBindingPartiallyBound")
        || !requireFeature(supportedVulkan12Features.descriptorBindingVariableDescriptorCount, "descriptorBindingVariableDescriptorCount")
        || !requireFeature(supportedVulkan12Features.timelineSemaphore, "timelineSemaphore")
        || !requireFeature(supportedVulkan12Features.shaderSampledImageArrayNonUniformIndexing, "shaderSampledImageArrayNonUniformIndexing")
        || !requireFeature(supportedVulkan12Features.shaderSubgroupExtendedTypes, "shaderSubgroupExtendedTypes")
        || !requireFeature(supportedVulkan12Features.scalarBlockLayout, "scalarBlockLayout")
        || !requireFeature(dynamicRenderingEnabled ? dynamicRenderingFeatures.dynamicRendering : VK_FALSE, "dynamicRendering")
        || !requireFeature(synchronization2Enabled ? synchronization2Features.synchronization2 : VK_FALSE, "synchronization2")
    )
        return false;

    m_dynamicRenderingSupported = true;
    m_synchronization2Supported = true;
    VulkanDetail::FinalizeOptionalDeviceFeatureEnablement(requestedOptionalFeatures, supportedOptionalFeatures);
    m_meshTaskShaderSupported =
        isDeviceExtensionEnabled(VK_EXT_MESH_SHADER_EXTENSION_NAME)
        && requestedOptionalFeatures.meshShader.taskShader == VK_TRUE
    ;
    m_rayTracingSpheresSupported =
        isDeviceExtensionEnabled(VK_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES_EXTENSION_NAME)
        && requestedOptionalFeatures.rayTracingLinearSweptSpheres.spheres == VK_TRUE
    ;
    m_rayTracingLinearSweptSpheresSupported =
        isDeviceExtensionEnabled(VK_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES_EXTENSION_NAME)
        && requestedOptionalFeatures.rayTracingLinearSweptSpheres.linearSweptSpheres == VK_TRUE
    ;

    HashSet<i32, Hasher<i32>, EqualTo<i32>, Alloc::ScratchAllocator<i32>> uniqueQueueFamilies(0, Hasher<i32>(), EqualTo<i32>(), Alloc::ScratchAllocator<i32>(scratchArena));
    uniqueQueueFamilies.reserve(4u);
    uniqueQueueFamilies.insert(m_graphicsQueueFamily);

    if(!m_deviceParams.headlessDevice)
        uniqueQueueFamilies.insert(m_presentQueueFamily);
    if(m_deviceParams.enableComputeQueue)
        uniqueQueueFamilies.insert(m_computeQueueFamily);
    if(m_deviceParams.enableCopyQueue)
        uniqueQueueFamilies.insert(m_transferQueueFamily);

    f32 priority = 1.f;
    Vector<VkDeviceQueueCreateInfo, Alloc::ScratchAllocator<VkDeviceQueueCreateInfo>> queueDesc(uniqueQueueFamilies.size(), Alloc::ScratchAllocator<VkDeviceQueueCreateInfo>(scratchArena));
    usize queueIndex = 0u;
    for(i32 queueFamily : uniqueQueueFamilies){
        VkDeviceQueueCreateInfo queueInfo = {};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = static_cast<u32>(queueFamily);
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        queueDesc[queueIndex] = queueInfo;
        ++queueIndex;
    }

    VkPhysicalDeviceVulkan13Features vulkan13features = VulkanDetail::MakeVkFeatureStruct<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
    vulkan13features.synchronization2 = synchronization2Features.synchronization2;
    vulkan13features.maintenance4 = maintenance4Features.maintenance4;
    vulkan13features.dynamicRendering = dynamicRenderingFeatures.dynamicRendering;

    pNext = nullptr;
    bool enabledOptionalFeatures[kOptionalDeviceFeatureCount] = {};
    for(const auto& [_, feature] : m_enabledExtensions.device)
        VulkanDetail::AppendOptionalDeviceFeature(pNext, requestedOptionalFeatures, feature, enabledOptionalFeatures);

    if(!apiSupportsVulkan13 && dynamicRenderingEnabled)
        VulkanDetail::AppendFeatureStruct(pNext, &dynamicRenderingFeatures);

    if(!apiSupportsVulkan13 && synchronization2Enabled)
        VulkanDetail::AppendFeatureStruct(pNext, &synchronization2Features);

    if(coopVecExtensionEnabled && cooperativeVectorFeatures.cooperativeVector)
        VulkanDetail::AppendFeatureStruct(pNext, &cooperativeVectorFeatures);

    if(apiSupportsVulkan13)
        VulkanDetail::AppendFeatureStruct(pNext, &vulkan13features);
    else if(maintenance4Enabled && maintenance4Features.maintenance4 == VK_TRUE)
        VulkanDetail::AppendFeatureStruct(pNext, &maintenance4Features);

    VkPhysicalDeviceFeatures coreDeviceFeatures = {};
    coreDeviceFeatures.shaderImageGatherExtended = supportedCoreFeatures.shaderImageGatherExtended;
    coreDeviceFeatures.samplerAnisotropy = supportedCoreFeatures.samplerAnisotropy;
    coreDeviceFeatures.tessellationShader = supportedCoreFeatures.tessellationShader;
    coreDeviceFeatures.textureCompressionBC = supportedCoreFeatures.textureCompressionBC;
    coreDeviceFeatures.geometryShader = supportedCoreFeatures.geometryShader;
    coreDeviceFeatures.imageCubeArray = supportedCoreFeatures.imageCubeArray;
    coreDeviceFeatures.shaderInt16 = supportedCoreFeatures.shaderInt16;
    coreDeviceFeatures.depthClamp = supportedCoreFeatures.depthClamp;
    coreDeviceFeatures.fillModeNonSolid = supportedCoreFeatures.fillModeNonSolid;
    coreDeviceFeatures.fragmentStoresAndAtomics = supportedCoreFeatures.fragmentStoresAndAtomics;
    coreDeviceFeatures.dualSrcBlend = supportedCoreFeatures.dualSrcBlend;
    coreDeviceFeatures.independentBlend = supportedCoreFeatures.independentBlend;
    coreDeviceFeatures.vertexPipelineStoresAndAtomics = supportedCoreFeatures.vertexPipelineStoresAndAtomics;
    coreDeviceFeatures.shaderInt64 = supportedCoreFeatures.shaderInt64;
    coreDeviceFeatures.shaderStorageImageWriteWithoutFormat = supportedCoreFeatures.shaderStorageImageWriteWithoutFormat;
    coreDeviceFeatures.shaderStorageImageReadWithoutFormat = supportedCoreFeatures.shaderStorageImageReadWithoutFormat;

    VkPhysicalDeviceVulkan11Features vulkan11features = VulkanDetail::MakeVkFeatureStruct<VkPhysicalDeviceVulkan11Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES);
    vulkan11features.storageBuffer16BitAccess = supportedVulkan11Features.storageBuffer16BitAccess;
    vulkan11features.pNext = pNext;

    VkPhysicalDeviceVulkan12Features vulkan12features = VulkanDetail::MakeVkFeatureStruct<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
    vulkan12features.descriptorIndexing = supportedVulkan12Features.descriptorIndexing;
    vulkan12features.runtimeDescriptorArray = supportedVulkan12Features.runtimeDescriptorArray;
    vulkan12features.descriptorBindingPartiallyBound = supportedVulkan12Features.descriptorBindingPartiallyBound;
    vulkan12features.descriptorBindingVariableDescriptorCount = supportedVulkan12Features.descriptorBindingVariableDescriptorCount;
    vulkan12features.timelineSemaphore = supportedVulkan12Features.timelineSemaphore;
    vulkan12features.shaderSampledImageArrayNonUniformIndexing = supportedVulkan12Features.shaderSampledImageArrayNonUniformIndexing;
    vulkan12features.bufferDeviceAddress = bufferDeviceAddressFeatures.bufferDeviceAddress;
    vulkan12features.shaderSubgroupExtendedTypes = supportedVulkan12Features.shaderSubgroupExtendedTypes;
    vulkan12features.scalarBlockLayout = supportedVulkan12Features.scalarBlockLayout;
    if(isDeviceExtensionEnabled(VK_EXT_SAMPLER_FILTER_MINMAX_EXTENSION_NAME))
        vulkan12features.samplerFilterMinmax = supportedVulkan12Features.samplerFilterMinmax;
    vulkan12features.pNext = &vulkan11features;

    auto extVec = VulkanDetail::StringMapKeysToVector(m_enabledExtensions.device, scratchArena);

    VkDeviceCreateInfo deviceCreateInfo = {};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.pQueueCreateInfos = queueDesc.data();
    deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueDesc.size());
    deviceCreateInfo.pEnabledFeatures = &coreDeviceFeatures;
    deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(extVec.size());
    deviceCreateInfo.ppEnabledExtensionNames = extVec.data();
    deviceCreateInfo.enabledLayerCount = 0;
    deviceCreateInfo.ppEnabledLayerNames = nullptr;
    deviceCreateInfo.pNext = &vulkan12features;

    res = vkCreateDevice(m_vulkanPhysicalDevice, &deviceCreateInfo, nullptr, &m_vulkanDevice);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create logical device. {}"), ResultToString(res));
        return false;
    }

    volkLoadDevice(m_vulkanDevice);

    vkGetDeviceQueue(m_vulkanDevice, static_cast<uint32_t>(m_graphicsQueueFamily), s_GraphicsQueueIndex, &m_graphicsQueue);
    if(m_deviceParams.enableComputeQueue)
        vkGetDeviceQueue(m_vulkanDevice, static_cast<uint32_t>(m_computeQueueFamily), s_ComputeQueueIndex, &m_computeQueue);
    if(m_deviceParams.enableCopyQueue)
        vkGetDeviceQueue(m_vulkanDevice, static_cast<uint32_t>(m_transferQueueFamily), s_TransferQueueIndex, &m_transferQueue);
    if(!m_deviceParams.headlessDevice)
        vkGetDeviceQueue(m_vulkanDevice, static_cast<uint32_t>(m_presentQueueFamily), s_PresentQueueIndex, &m_presentQueue);

    m_bufferDeviceAddressSupported = vulkan12features.bufferDeviceAddress == VK_TRUE;

    if(m_deviceParams.enableDebugRuntime){
        const u64 deviceLocalMemoryMiB = VulkanDetail::BytesToMiB(VulkanDetail::GetDeviceLocalMemoryBytes(m_vulkanPhysicalDevice));

        AStringStream ss;
        ss << "Vulkan GPU debug: selected device"
           << "\n    name: " << physicalDeviceProperties.deviceName
           << "\n    type: " << VulkanDetail::PhysicalDeviceTypeToString(physicalDeviceProperties.deviceType)
           << "\n    vendor/device id: 0x" << StreamHex << physicalDeviceProperties.vendorID << "/0x" << physicalDeviceProperties.deviceID << StreamDec
           << "\n    Vulkan API: " << VulkanDetail::VulkanVersionToString(physicalDeviceProperties.apiVersion)
           << "\n    driver version: " << physicalDeviceProperties.driverVersion
           << "\n    device-local memory: " << deviceLocalMemoryMiB << " MiB"
           << "\n    queue families: graphics=" << m_graphicsQueueFamily
        ;

        if(m_deviceParams.headlessDevice)
            ss << " present=headless";
        else
            ss << " present=" << m_presentQueueFamily;

        if(m_deviceParams.enableComputeQueue)
            ss << " compute=" << m_computeQueueFamily;
        else
            ss << " compute=not-requested";

        if(m_deviceParams.enableCopyQueue)
            ss << " transfer=" << m_transferQueueFamily;
        else
            ss << " transfer=not-requested";

        ss << "\n    key features: dynamicRendering=" << VulkanDetail::BoolToString(m_dynamicRenderingSupported)
           << " synchronization2=" << VulkanDetail::BoolToString(m_synchronization2Supported)
           << " bufferDeviceAddress=" << VulkanDetail::BoolToString(m_bufferDeviceAddressSupported)
           << " meshTaskShader=" << VulkanDetail::BoolToString(m_meshTaskShaderSupported)
           << " maintenance4=" << VulkanDetail::BoolToString(maintenance4Enabled && maintenance4Features.maintenance4 == VK_TRUE)
           << "\n    optional paths: debugMarker=" << VulkanDetail::BoolToString(isDeviceExtensionEnabled(VK_EXT_DEBUG_MARKER_EXTENSION_NAME))
           << " descriptorHeap=" << VulkanDetail::BoolToString(isDeviceExtensionEnabled(VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME))
           << " meshShader=" << VulkanDetail::BoolToString(isDeviceExtensionEnabled(VK_EXT_MESH_SHADER_EXTENSION_NAME))
           << " rayTracingPipeline=" << VulkanDetail::BoolToString(isDeviceExtensionEnabled(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME))
           << " rayQuery=" << VulkanDetail::BoolToString(isDeviceExtensionEnabled(VK_KHR_RAY_QUERY_EXTENSION_NAME))
           << " spheres=" << VulkanDetail::BoolToString(m_rayTracingSpheresSupported)
           << " linearSweptSpheres=" << VulkanDetail::BoolToString(m_rayTracingLinearSweptSpheresSupported)
           << " cooperativeVector=" << VulkanDetail::BoolToString(coopVecExtensionEnabled && cooperativeVectorFeatures.cooperativeVector == VK_TRUE)
           << "\n    enabled device extensions: " << m_enabledExtensions.device.size()
        ;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("{}"), StringConvert(ss.str()));
    }

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Vulkan: created device '{}'"), m_rendererString);

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Window surface creation


bool BackendContext::createWindowSurface(){
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
#elif defined(NWB_PLATFORM_LINUX)
    Common::LinuxFrame frame;
    frame.frameParam() = m_platformFrameParam;

    switch(frame.backend()){
    case Common::LinuxFrameBackend::Enum::X11:
    {
        VkXlibSurfaceCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
        createInfo.dpy = reinterpret_cast<decltype(createInfo.dpy)>(frame.nativeDisplay());
        createInfo.window = static_cast<decltype(createInfo.window)>(frame.nativeWindowHandle());

        res = vkCreateXlibSurfaceKHR(m_vulkanInstance, &createInfo, nullptr, &m_windowSurface);
        if(res != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create Xlib surface. {}"), ResultToString(res));
            return false;
        }
        return true;
    }
#if defined(NWB_WITH_WAYLAND)
    case Common::LinuxFrameBackend::Enum::Wayland:
    {
        VkWaylandSurfaceCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        createInfo.display = reinterpret_cast<decltype(createInfo.display)>(frame.nativeDisplay());
        createInfo.surface = reinterpret_cast<decltype(createInfo.surface)>(static_cast<usize>(frame.nativeWindowHandle()));

        res = vkCreateWaylandSurfaceKHR(m_vulkanInstance, &createInfo, nullptr, &m_windowSurface);
        if(res != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create Wayland surface. {}"), ResultToString(res));
            return false;
        }
        return true;
    }
#endif
    case Common::LinuxFrameBackend::Enum::None:
    default:
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Unsupported Linux window backend for surface creation."));
        return false;
    }
#else
    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Surface creation not supported on this platform."));
    return false;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Swap chain management


void BackendContext::destroySwapChain(){
    if(m_vulkanDevice)
        vkDeviceWaitIdle(m_vulkanDevice);

    if(m_swapChain){
        vkDestroySwapchainKHR(m_vulkanDevice, m_swapChain, nullptr);
        m_swapChain = VK_NULL_HANDLE;
    }

    m_swapChainImages.clear();
}

bool BackendContext::createVulkanSwapChain(){
    VkResult res = VK_SUCCESS;

    destroySwapChain();

    m_swapChainFormat.format = VulkanDetail::ConvertFormat(m_swapChainState.backBufferFormat);
    if(m_swapChainFormat.format == VK_FORMAT_UNDEFINED){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create swapchain: back buffer format is unsupported"));
        return false;
    }
    m_swapChainFormat.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

    VkSurfaceCapabilitiesKHR surfaceCaps = {};
    res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_vulkanPhysicalDevice, m_windowSurface, &surfaceCaps);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to query surface capabilities. {}"), ResultToString(res));
        return false;
    }

    VkExtent2D extent = {};
    if(surfaceCaps.currentExtent.width != UINT32_MAX && surfaceCaps.currentExtent.height != UINT32_MAX){
        extent = surfaceCaps.currentExtent;
    }
    else{
        extent.width = Max(surfaceCaps.minImageExtent.width, Min(surfaceCaps.maxImageExtent.width, m_swapChainState.backBufferWidth));
        extent.height = Max(surfaceCaps.minImageExtent.height, Min(surfaceCaps.maxImageExtent.height, m_swapChainState.backBufferHeight));
    }
    if(extent.width == 0 || extent.height == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Surface extent is invalid ({}x{})."), extent.width, extent.height);
        return false;
    }

    m_swapChainState.backBufferWidth = extent.width;
    m_swapChainState.backBufferHeight = extent.height;

    uint32_t presentModeCount = 0;
    res = vkGetPhysicalDeviceSurfacePresentModesKHR(m_vulkanPhysicalDevice, m_windowSurface, &presentModeCount, nullptr);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate present mode count. {}"), ResultToString(res));
        return false;
    }

    Alloc::ScratchArena<> scratchArena;

    Vector<VkPresentModeKHR, Alloc::ScratchAllocator<VkPresentModeKHR>> presentModes(presentModeCount, Alloc::ScratchAllocator<VkPresentModeKHR>(scratchArena));
    res = vkGetPhysicalDeviceSurfacePresentModesKHR(m_vulkanPhysicalDevice, m_windowSurface, &presentModeCount, presentModes.data());
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate present modes. {}"), ResultToString(res));
        return false;
    }

    const VkPresentModeKHR requestedPresentMode = m_swapChainState.vsyncEnabled ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_IMMEDIATE_KHR;
    VkPresentModeKHR selectedPresentMode = VK_PRESENT_MODE_FIFO_KHR;
    bool requestedPresentModeFound = false;
    bool fifoPresentModeFound = false;
    for(const auto& mode : presentModes){
        if(mode == requestedPresentMode){
            selectedPresentMode = requestedPresentMode;
            requestedPresentModeFound = true;
            break;
        }
        if(mode == VK_PRESENT_MODE_FIFO_KHR)
            fifoPresentModeFound = true;
    }
    if(!requestedPresentModeFound && !fifoPresentModeFound){
        if(presentModes.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Surface exposes no present modes."));
            return false;
        }
        selectedPresentMode = presentModes[0];
    }

    uint32_t selectedImageCount = Max(m_deviceParams.swapChainBufferCount, surfaceCaps.minImageCount);
    if(surfaceCaps.maxImageCount > 0)
        selectedImageCount = Min(selectedImageCount, surfaceCaps.maxImageCount);

    const VkSurfaceTransformFlagBitsKHR selectedPreTransform = (surfaceCaps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
        ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
        : surfaceCaps.currentTransform
    ;

    VkCompositeAlphaFlagBitsKHR selectedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    bool compositeAlphaFound = false;
    const VkCompositeAlphaFlagBitsKHR compositeAlphaCandidates[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
    };
    for(const auto candidate : compositeAlphaCandidates){
        if(surfaceCaps.supportedCompositeAlpha & candidate){
            selectedCompositeAlpha = candidate;
            compositeAlphaFound = true;
            break;
        }
    }
    if(!compositeAlphaFound){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Surface supports no compatible composite alpha mode."));
        return false;
    }

    uint32_t queueFamilyIndices[2] = { static_cast<uint32_t>(m_graphicsQueueFamily), static_cast<uint32_t>(m_presentQueueFamily) };
    uint32_t queueFamilyIndexCount = 1;
    if(m_presentQueueFamily != m_graphicsQueueFamily){
        queueFamilyIndices[queueFamilyIndexCount] = static_cast<uint32_t>(m_presentQueueFamily);
        ++queueFamilyIndexCount;
    }
    const bool enableSwapChainSharing = queueFamilyIndexCount > 1;

    VkSwapchainCreateInfoKHR desc = {};
    desc.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    desc.surface = m_windowSurface;
    desc.minImageCount = selectedImageCount;
    desc.imageFormat = m_swapChainFormat.format;
    desc.imageColorSpace = m_swapChainFormat.colorSpace;
    desc.imageExtent = extent;
    desc.imageArrayLayers = 1;
    desc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    desc.imageSharingMode = enableSwapChainSharing ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
    desc.queueFamilyIndexCount = enableSwapChainSharing ? queueFamilyIndexCount : 0;
    desc.pQueueFamilyIndices = enableSwapChainSharing ? queueFamilyIndices : nullptr;
    desc.preTransform = selectedPreTransform;
    desc.compositeAlpha = selectedCompositeAlpha;
    desc.presentMode = selectedPresentMode;
    desc.clipped = VK_TRUE;
    desc.oldSwapchain = VK_NULL_HANDLE;

    if(m_swapChainMutableFormatSupported)
        desc.flags |= VK_SWAPCHAIN_CREATE_MUTABLE_FORMAT_BIT_KHR;

    VkFormat imageFormats[s_MaxMutableSwapChainFormats] = { m_swapChainFormat.format, VK_FORMAT_UNDEFINED };
    uint32_t imageFormatCount = 1;
    switch(m_swapChainFormat.format){
    case VK_FORMAT_R8G8B8A8_UNORM: imageFormats[1] = VK_FORMAT_R8G8B8A8_SRGB; imageFormatCount = s_MaxMutableSwapChainFormats; break;
    case VK_FORMAT_R8G8B8A8_SRGB:  imageFormats[1] = VK_FORMAT_R8G8B8A8_UNORM; imageFormatCount = s_MaxMutableSwapChainFormats; break;
    case VK_FORMAT_B8G8R8A8_UNORM: imageFormats[1] = VK_FORMAT_B8G8R8A8_SRGB; imageFormatCount = s_MaxMutableSwapChainFormats; break;
    case VK_FORMAT_B8G8R8A8_SRGB:  imageFormats[1] = VK_FORMAT_B8G8R8A8_UNORM; imageFormatCount = s_MaxMutableSwapChainFormats; break;
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
    res = vkGetSwapchainImagesKHR(m_vulkanDevice, m_swapChain, &imageCount, nullptr);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to query swap chain image count. {}"), ResultToString(res));
        vkDestroySwapchainKHR(m_vulkanDevice, m_swapChain, nullptr);
        m_swapChain = VK_NULL_HANDLE;
        m_swapChainImages.clear();
        return false;
    }

    if(imageCount == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Swap chain reported zero images."));
        vkDestroySwapchainKHR(m_vulkanDevice, m_swapChain, nullptr);
        m_swapChain = VK_NULL_HANDLE;
        m_swapChainImages.clear();
        return false;
    }

    Vector<VkImage, Alloc::ScratchAllocator<VkImage>> images(imageCount, Alloc::ScratchAllocator<VkImage>(scratchArena));
    res = vkGetSwapchainImagesKHR(m_vulkanDevice, m_swapChain, &imageCount, images.data());
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to retrieve swap chain images. {}"), ResultToString(res));
        vkDestroySwapchainKHR(m_vulkanDevice, m_swapChain, nullptr);
        m_swapChain = VK_NULL_HANDLE;
        m_swapChainImages.clear();
        return false;
    }

    m_swapChainImages.resize(imageCount);
    for(uint32_t imageIndex = 0; imageIndex < imageCount; ++imageIndex){
        const VkImage image = images[imageIndex];
        SwapChainImage& sci = m_swapChainImages[imageIndex];
        sci.image = image;

        TextureDesc textureDesc;
        textureDesc.width = m_swapChainState.backBufferWidth;
        textureDesc.height = m_swapChainState.backBufferHeight;
        textureDesc.format = m_swapChainState.backBufferFormat;
        textureDesc.initialState = ResourceStates::Present;
        textureDesc.keepInitialState = true;
        textureDesc.isRenderTarget = true;

        sci.rhiHandle = m_rhiDevice->createHandleForNativeTexture(ObjectTypes::VK_Image, Object(sci.image), textureDesc);
        if(!sci.rhiHandle){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create RHI handle for a swap chain image."));
            m_swapChainImages.clear();
            vkDestroySwapchainKHR(m_vulkanDevice, m_swapChain, nullptr);
            m_swapChain = VK_NULL_HANDLE;
            return false;
        }
        checked_cast<Texture*>(sci.rhiHandle.get())->m_keepInitialStateKnown = false;
    }

    m_swapChainIndex = 0;

    if(m_deviceParams.enableDebugRuntime){
        AStringStream ss;
        ss << "Vulkan GPU debug: swap chain"
           << "\n    extent: " << extent.width << "x" << extent.height
           << "\n    format: " << VulkanDetail::SwapChainFormatToString(m_swapChainFormat.format)
           << " (" << static_cast<i32>(m_swapChainFormat.format) << ")"
           << "\n    color space: " << VulkanDetail::ColorSpaceToString(m_swapChainFormat.colorSpace)
           << " (" << static_cast<i32>(m_swapChainFormat.colorSpace) << ")"
           << "\n    present mode: " << VulkanDetail::PresentModeToString(selectedPresentMode)
           << " (" << static_cast<i32>(selectedPresentMode) << ")"
           << "\n    requested images: " << m_deviceParams.swapChainBufferCount
           << "\n    created images: " << imageCount
           << "\n    mutable format: " << VulkanDetail::BoolToString(m_swapChainMutableFormatSupported)
           << "\n    queue sharing: " << (enableSwapChainSharing ? "concurrent" : "exclusive")
        ;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("{}"), StringConvert(ss.str()));
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// High-level lifecycle methods


bool BackendContext::createInstance(){
    initDefaultExtensions();

    if(m_deviceParams.enableDebugRuntime){
        m_enabledExtensions.instance.insert(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
        m_enabledExtensions.layers.insert("VK_LAYER_KHRONOS_validation");
    }

    return createVulkanInstance();
}

bool BackendContext::createDevice(){
    if(m_deviceParams.enableDebugRuntime)
        installDebugCallback();

    m_maxFramesInFlight = m_deviceParams.maxFramesInFlight;
    if(m_maxFramesInFlight == 0){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: maxFramesInFlight was 0; clamping to 1."));
        m_maxFramesInFlight = 1;
    }

    auto resolveDeviceExtensionFeature = [this](const AString& name)->DeviceExtensionFeature::Enum{
        const auto optionalIt = m_optionalExtensions.device.find(name);
        if(optionalIt != m_optionalExtensions.device.end() && optionalIt.value() != DeviceExtensionFeature::None)
            return optionalIt.value();

        const auto rayTracingIt = m_rayTracingExtensions.find(name);
        if(rayTracingIt != m_rayTracingExtensions.end())
            return rayTracingIt.value();

        return DeviceExtensionFeature::None;
    };

    auto registerDeviceExtension = [](DeviceExtensionMap& extensions, const AString& name, const DeviceExtensionFeature::Enum feature){
        auto [it, inserted] = extensions.emplace(name, feature);
        if(!inserted && it.value() == DeviceExtensionFeature::None && feature != DeviceExtensionFeature::None)
            it.value() = feature;
    };

    for(const auto& name : m_deviceParams.requiredVulkanDeviceExtensions)
        registerDeviceExtension(m_enabledExtensions.device, name, resolveDeviceExtensionFeature(name));
    for(const auto& name : m_deviceParams.optionalVulkanDeviceExtensions)
        registerDeviceExtension(m_optionalExtensions.device, name, resolveDeviceExtensionFeature(name));
    if(m_deviceParams.enableAftermath)
        m_optionalExtensions.device.emplace(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME, DeviceExtensionFeature::None);

    m_swapChainState.backBufferFormat = VulkanDetail::GetBackBufferFormat(m_deviceParams);
    if(!m_deviceParams.headlessDevice)
        m_enabledExtensions.device.emplace(VK_KHR_SWAPCHAIN_EXTENSION_NAME, DeviceExtensionFeature::None);
    if(!m_deviceParams.headlessDevice){
        if(!createWindowSurface())
            return false;
    }
    if(!pickPhysicalDevice())
        return false;
    if(!findQueueFamilies(m_vulkanPhysicalDevice))
        return false;
    if(!createVulkanDevice())
        return false;

    Alloc::ScratchArena<> scratchArena(32768);

    auto vecInstanceExt = VulkanDetail::StringSetToVector(m_enabledExtensions.instance, scratchArena);
    auto vecDeviceExt = VulkanDetail::StringMapKeysToVector(m_enabledExtensions.device, scratchArena);
    const bool aftermathCheckpointsEnabled = isDeviceExtensionEnabled(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);
    if(m_deviceParams.enableAftermath && !aftermathCheckpointsEnabled)
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Aftermath marker checkpoints are disabled because VK_NV_device_diagnostic_checkpoints is unavailable."));

    DeviceDesc deviceDesc(m_allocator, m_threadPool);
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
    deviceDesc.dynamicRenderingSupported = m_dynamicRenderingSupported;
    deviceDesc.synchronization2Supported = m_synchronization2Supported;
    deviceDesc.meshTaskShaderSupported = m_meshTaskShaderSupported;
    deviceDesc.rayTracingSpheresSupported = m_rayTracingSpheresSupported;
    deviceDesc.rayTracingLinearSweptSpheresSupported = m_rayTracingLinearSweptSpheresSupported;
    deviceDesc.aftermathEnabled = m_deviceParams.enableAftermath && aftermathCheckpointsEnabled;
    deviceDesc.logBufferLifetime = m_deviceParams.logBufferLifetime;
    deviceDesc.vulkanLibraryName = m_deviceParams.vulkanLibraryName;
    deviceDesc.pipelineCacheDirectory = m_deviceParams.pipelineCacheDirectory;
    deviceDesc.systemMemoryAllocator = &m_allocator.getSystemMemoryAllocator();

    m_rhiDevice = CreateDevice(deviceDesc);
    if(!m_rhiDevice){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create RHI device wrapper."));
        return false;
    }

    return true;
}

bool BackendContext::createSwapChain(){
    if(!createVulkanSwapChain())
        return false;

    usize const numPresentSemaphores = m_swapChainImages.size();
    if(!recreateSemaphores(m_presentSemaphores, numPresentSemaphores, "create present semaphores")){
        destroySwapChain();
        return false;
    }

    usize const numAcquireSemaphores = Max(static_cast<usize>(m_maxFramesInFlight), m_swapChainImages.size());
    if(!recreateSemaphores(m_acquireSemaphores, numAcquireSemaphores, "create acquire semaphores")){
        clearSemaphores(m_presentSemaphores);
        destroySwapChain();
        return false;
    }

    m_swapChainIndex = 0;
    m_acquireSemaphoreIndex = 0;

    return true;
}

void BackendContext::destroy(){
    if(m_rhiDevice)
        m_rhiDevice->waitForIdle();

    while(!m_framesInFlight.empty())
        m_framesInFlight.pop();
    m_queryPool.clear();

    destroySwapChain();

    clearSemaphores(m_presentSemaphores);
    clearSemaphores(m_acquireSemaphores);

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


bool BackendContext::beginFrame(const BackBufferResizeCallbacks& callbacks){
    VkResult res = VK_SUCCESS;
    VkSemaphore semaphore = VK_NULL_HANDLE;

    if(!m_swapChain || m_acquireSemaphores.empty()){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: beginFrame skipped because swap chain or acquire semaphores are not ready."));
        return false;
    }

    for(usize attempt = 0; attempt < s_MaxRetryCountAcquireNextImage; ++attempt){
        if(!m_swapChain || m_acquireSemaphores.empty()){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: beginFrame aborted because swap chain or acquire semaphores are unavailable."));
            return false;
        }

        if(m_acquireSemaphoreIndex >= m_acquireSemaphores.size())
            m_acquireSemaphoreIndex = 0;

        semaphore = m_acquireSemaphores[m_acquireSemaphoreIndex];

        res = vkAcquireNextImageKHR(
            m_vulkanDevice,
            m_swapChain,
            UINT64_MAX,
            semaphore,
            VK_NULL_HANDLE,
            &m_swapChainIndex
        );

        if((res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) && attempt < s_MaxRetryCountAcquireNextImage - 1){
            if(callbacks.beforeResize)
                callbacks.beforeResize(callbacks.userData);

            VkSurfaceCapabilitiesKHR surfaceCaps;
            res = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_vulkanPhysicalDevice, m_windowSurface, &surfaceCaps);
            if(res != VK_SUCCESS){
                NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to query surface capabilities during resize. {}"), ResultToString(res));
                return false;
            }

            if(surfaceCaps.currentExtent.width != UINT32_MAX && surfaceCaps.currentExtent.height != UINT32_MAX){
                m_swapChainState.backBufferWidth = surfaceCaps.currentExtent.width;
                m_swapChainState.backBufferHeight = surfaceCaps.currentExtent.height;
            }
            else{
                m_swapChainState.backBufferWidth = Max(surfaceCaps.minImageExtent.width, Min(surfaceCaps.maxImageExtent.width, m_swapChainState.backBufferWidth));
                m_swapChainState.backBufferHeight = Max(surfaceCaps.minImageExtent.height, Min(surfaceCaps.maxImageExtent.height, m_swapChainState.backBufferHeight));
            }

            resizeSwapChain();
            if(callbacks.afterResize)
                callbacks.afterResize(callbacks.userData);
        }
        else
            break;
    }

    if(m_acquireSemaphores.empty()){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: beginFrame aborted because acquire semaphore pool became empty."));
        return false;
    }

    if(res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR){
        m_acquireSemaphoreIndex = (m_acquireSemaphoreIndex + 1) % static_cast<uint32_t>(m_acquireSemaphores.size());
        m_rhiDevice->queueWaitForSemaphore(CommandQueue::Graphics, semaphore, 0);
        return true;
    }

    NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to acquire next swap chain image. {}"), ResultToString(res));
    return false;
}

bool BackendContext::present(){
    VkResult res = VK_SUCCESS;

    if(!m_swapChain || m_presentSemaphores.empty() || m_swapChainImages.empty()){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: present skipped because swap chain or present semaphores are not ready."));
        return false;
    }

    if(m_swapChainIndex >= m_presentSemaphores.size() || m_swapChainIndex >= m_swapChainImages.size())
        m_swapChainIndex = 0;

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
    if(!(res == VK_SUCCESS || res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Queue present failed. {}"), ResultToString(res));
        return false;
    }

    while(m_framesInFlight.size() >= m_maxFramesInFlight){
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

    if(!query){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to acquire frame synchronization query; continuing without frame fence throttling."));
        return true;
    }

    m_rhiDevice->resetEventQuery(query.get());
    m_rhiDevice->setEventQuery(query.get(), CommandQueue::Graphics);
    m_framesInFlight.push(query);

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Adapter enumeration


bool BackendContext::enumerateAdapters(Vector<AdapterInfo>& outAdapters){
    VkResult res = VK_SUCCESS;

    if(!m_vulkanInstance){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate adapters: instance is null"));
        return false;
    }

    uint32_t deviceCount = 0;
    res = vkEnumeratePhysicalDevices(m_vulkanInstance, &deviceCount, nullptr);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate adapter count. {}"), ResultToString(res));
        return false;
    }

    if(deviceCount == 0){
        outAdapters.clear();
        return true;
    }

    Alloc::ScratchArena<> scratchArena;

    Vector<VkPhysicalDevice, Alloc::ScratchAllocator<VkPhysicalDevice>> devices(deviceCount, Alloc::ScratchAllocator<VkPhysicalDevice>(scratchArena));
    res = vkEnumeratePhysicalDevices(m_vulkanInstance, &deviceCount, devices.data());
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to enumerate adapters. {}"), ResultToString(res));
        return false;
    }

    outAdapters.clear();
    outAdapters.resize(deviceCount);

    auto fillAdapterInfo = [&](usize i){
        auto* physicalDevice = devices[i];
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

        outAdapters[i] = Move(adapterInfo);
    };

    if(m_threadPool.isParallelEnabled() && deviceCount >= s_ParallelAdapterThreshold)
        m_threadPool.parallelFor(static_cast<usize>(0), static_cast<usize>(deviceCount), fillAdapterInfo);
    else{
        for(usize i = 0; i < static_cast<usize>(deviceCount); ++i)
            fillAdapterInfo(i);
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

