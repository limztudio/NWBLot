// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend_context.h"
#include "arena_names.h"
#include "aftermath.h"
#include "swapchain_presentation.h"

#include <core/common/log.h>

#include <sstream>

#ifdef NWB_PLATFORM_WINDOWS
#include <windows.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ScratchString = AString<Alloc::ScratchArena>;
using ScratchStringStream = AStringStream<Alloc::ScratchArena>;
using ScratchStringSet = HashSet<ScratchString, Hasher<ScratchString>, EqualTo<ScratchString>, Alloc::ScratchArena>;

static constexpr u64 s_BytesPerMiB = 1024ull * 1024ull;
// HDR10 metadata mirrors the Rec.2020/ST.2084 presentation transform. Keep the mastering display values named so
// a presentation-policy adjustment cannot silently leave Vulkan's advertised metadata behind.
static constexpr VkXYColorEXT s_Hdr10DisplayPrimaryRed = { 0.708f, 0.292f };
static constexpr VkXYColorEXT s_Hdr10DisplayPrimaryGreen = { 0.170f, 0.797f };
static constexpr VkXYColorEXT s_Hdr10DisplayPrimaryBlue = { 0.131f, 0.046f };
static constexpr VkXYColorEXT s_Hdr10WhitePointD65 = { 0.3127f, 0.3290f };
static constexpr f32 s_Hdr10MasteringPeakLuminance = 1000.0f;
static constexpr f32 s_Hdr10MinimumLuminance = 0.005f;
static constexpr f32 s_Hdr10MaximumFrameAverageLightLevel = 400.0f;
static constexpr u32 s_Hdr10MetadataSwapChainCount = 1u;

// Vulkan guarantees transfer support for Graphics and Compute families, but a Graphics family does not imply
// Compute support. Keep the physical registry faithful to the family flags so cross-family Graphics queues cannot
// receive Compute work merely because the primary Graphics family can execute it.
[[nodiscard]] inline constexpr GpuQueueCapability::Mask QueueCapabilitiesForQueueFlags(
    const VkQueueFlags queueFlags
)noexcept{
    u8 capabilities = 0u;
    if((queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u){
        capabilities |= static_cast<u8>(GpuQueueCapability::Graphics);
        capabilities |= static_cast<u8>(GpuQueueCapability::Transfer);
    }
    if((queueFlags & VK_QUEUE_COMPUTE_BIT) != 0u){
        capabilities |= static_cast<u8>(GpuQueueCapability::Compute);
        capabilities |= static_cast<u8>(GpuQueueCapability::Transfer);
    }
    if((queueFlags & VK_QUEUE_TRANSFER_BIT) != 0u)
        capabilities |= static_cast<u8>(GpuQueueCapability::Transfer);
    return static_cast<GpuQueueCapability::Mask>(capabilities);
}

inline ScratchStringStream MakeScratchStringStream(Alloc::ScratchArena& arena){
    return ScratchStringStream(std::ios_base::out, arena);
}

inline ScratchString MakeScratchString(Alloc::ScratchArena& arena, const AStringView text){
    return ScratchString(text, arena);
}

template<typename Set>
static Vector<const char*, Alloc::ScratchArena> StringSetToVector(const Set& set, Alloc::ScratchArena& arena){
    Vector<const char*, Alloc::ScratchArena> ret(arena);
    ret.reserve(set.size());
    for(const auto& s : set)
        ret.push_back(s.c_str());
    return ret;
}

template<typename Map>
static Vector<const char*, Alloc::ScratchArena> StringMapKeysToVector(const Map& map, Alloc::ScratchArena& arena){
    Vector<const char*, Alloc::ScratchArena> ret(arena);
    ret.reserve(map.size());
    for(const auto& [key, val] : map){
        static_cast<void>(val);
        ret.push_back(key.c_str());
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
    VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT rayTracingInvocationReorderExt = MakeVkFeatureStruct<VkPhysicalDeviceRayTracingInvocationReorderFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_INVOCATION_REORDER_FEATURES_EXT);
    VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV rayTracingLinearSweptSpheres = MakeVkFeatureStruct<VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_LINEAR_SWEPT_SPHERES_FEATURES_NV);
    VkPhysicalDeviceMeshShaderFeaturesEXT meshShader = MakeVkFeatureStruct<VkPhysicalDeviceMeshShaderFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT);
    VkPhysicalDeviceFragmentShadingRateFeaturesKHR fragmentShadingRate = MakeVkFeatureStruct<VkPhysicalDeviceFragmentShadingRateFeaturesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR);
    // Descriptor buffers are the production descriptor transport.
    VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptorBuffer = MakeVkFeatureStruct<VkPhysicalDeviceDescriptorBufferFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT);
    VkPhysicalDeviceFaultFeaturesEXT deviceFault = MakeVkFeatureStruct<VkPhysicalDeviceFaultFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT);
    VkPhysicalDeviceTextureCompressionASTCHDRFeatures textureCompressionAstcHdr = MakeVkFeatureStruct<VkPhysicalDeviceTextureCompressionASTCHDRFeatures>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TEXTURE_COMPRESSION_ASTC_HDR_FEATURES);
};

inline OptionalDeviceFeatureSet MakeRequestedOptionalDeviceFeatures(){
    OptionalDeviceFeatureSet features;

    features.accelerationStructure.accelerationStructure = VK_TRUE;

    features.rayTracingPipeline.rayTracingPipeline = VK_TRUE;
    features.rayTracingPipeline.rayTraversalPrimitiveCulling = VK_TRUE;

    features.rayQuery.rayQuery = VK_TRUE;

    features.opacityMicromap.micromap = VK_TRUE;

    features.clusterAccelerationStructure.clusterAccelerationStructure = VK_TRUE;

    features.rayTracingInvocationReorder.rayTracingInvocationReorder = VK_TRUE;
    features.rayTracingInvocationReorderExt.rayTracingInvocationReorder = VK_TRUE;

    features.rayTracingLinearSweptSpheres.spheres = VK_FALSE;
    features.rayTracingLinearSweptSpheres.linearSweptSpheres = VK_FALSE;

    features.meshShader.meshShader = VK_TRUE;

    features.fragmentShadingRate.pipelineFragmentShadingRate = VK_TRUE;
    features.fragmentShadingRate.primitiveFragmentShadingRate = VK_TRUE;
    features.fragmentShadingRate.attachmentFragmentShadingRate = VK_TRUE;

    // Descriptor-buffer feature support is mandatory.
    features.descriptorBuffer.descriptorBuffer = VK_TRUE;

    features.deviceFault.deviceFault = VK_TRUE;
    features.deviceFault.deviceFaultVendorBinary = VK_TRUE;

    features.textureCompressionAstcHdr.textureCompressionASTC_HDR = VK_TRUE;

    return features;
}

inline void* GetOptionalDeviceFeatureStruct(OptionalDeviceFeatureSet& features, DeviceExtensionFeature::Enum feature){
    switch(feature){
    case DeviceExtensionFeature::AccelerationStructure: return &features.accelerationStructure;
    case DeviceExtensionFeature::RayTracingPipeline: return &features.rayTracingPipeline;
    case DeviceExtensionFeature::RayQuery: return &features.rayQuery;
    case DeviceExtensionFeature::OpacityMicromap: return &features.opacityMicromap;
    case DeviceExtensionFeature::ClusterAccelerationStructure: return &features.clusterAccelerationStructure;
    case DeviceExtensionFeature::RayTracingInvocationReorder: return &features.rayTracingInvocationReorder;
    case DeviceExtensionFeature::RayTracingInvocationReorderExt: return &features.rayTracingInvocationReorderExt;
    case DeviceExtensionFeature::RayTracingLinearSweptSpheres: return &features.rayTracingLinearSweptSpheres;
    case DeviceExtensionFeature::MeshShader: return &features.meshShader;
    case DeviceExtensionFeature::FragmentShadingRate: return &features.fragmentShadingRate;
    case DeviceExtensionFeature::DescriptorBuffer: return &features.descriptorBuffer;
    case DeviceExtensionFeature::DeviceFault: return &features.deviceFault;
    case DeviceExtensionFeature::TextureCompressionAstcHdr: return &features.textureCompressionAstcHdr;
    case DeviceExtensionFeature::None:
    case DeviceExtensionFeature::Count:
    default:
        return nullptr;
    }
}

inline Format::Enum GetBackBufferFormat(const DeviceCreationParameters& params){
    if(params.headlessDevice)
        return params.swapChainFormat;

    if(params.swapChainFormat == Format::RGBA8_UNORM_SRGB)
        return Format::BGRA8_UNORM_SRGB;
    if(params.swapChainFormat == Format::RGBA8_UNORM)
        return Format::BGRA8_UNORM;
    return params.swapChainFormat;
}

inline bool SupportsRequestedValue(VkBool32 requested, VkBool32 supported){
    return requested != VK_TRUE || supported == VK_TRUE;
}

inline const char* BoolToString(bool value){
    return value ? "yes" : "no";
}

inline ScratchString VulkanVersionToString(Alloc::ScratchArena& arena, u32 version){
    auto ss = MakeScratchStringStream(arena);
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

inline const char* PhysicalDeviceTypeToString(VkPhysicalDeviceType type){
    switch(type){
    case VK_PHYSICAL_DEVICE_TYPE_OTHER: return "other";
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated GPU";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "discrete GPU";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "virtual GPU";
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return "CPU";
    default: return "unknown";
    }
}

inline const char* SwapChainFormatToString(VkFormat format){
    switch(format){
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return "VK_FORMAT_A2B10G10R10_UNORM_PACK32";
    case VK_FORMAT_R8G8B8A8_UNORM: return "VK_FORMAT_R8G8B8A8_UNORM";
    case VK_FORMAT_R8G8B8A8_SRGB: return "VK_FORMAT_R8G8B8A8_SRGB";
    case VK_FORMAT_B8G8R8A8_UNORM: return "VK_FORMAT_B8G8R8A8_UNORM";
    case VK_FORMAT_B8G8R8A8_SRGB: return "VK_FORMAT_B8G8R8A8_SRGB";
    default: return "unknown";
    }
}

inline const char* ColorSpaceToString(VkColorSpaceKHR colorSpace){
    switch(colorSpace){
    case VK_COLOR_SPACE_HDR10_ST2084_EXT: return "VK_COLOR_SPACE_HDR10_ST2084_EXT";
    case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR: return "VK_COLOR_SPACE_SRGB_NONLINEAR_KHR";
    default: return "unknown";
    }
}

inline const char* PresentModeToString(VkPresentModeKHR mode){
    switch(mode){
    case VK_PRESENT_MODE_IMMEDIATE_KHR: return "VK_PRESENT_MODE_IMMEDIATE_KHR";
    case VK_PRESENT_MODE_MAILBOX_KHR: return "VK_PRESENT_MODE_MAILBOX_KHR";
    case VK_PRESENT_MODE_FIFO_KHR: return "VK_PRESENT_MODE_FIFO_KHR";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "VK_PRESENT_MODE_FIFO_RELAXED_KHR";
    default: return "unknown";
    }
}

inline void SetHdr10Metadata(const VkDevice device, const VkSwapchainKHR swapChain){
    if(!vkSetHdrMetadataEXT || !device || !swapChain)
        return;

    VkHdrMetadataEXT metadata = {};
    metadata.sType = VK_STRUCTURE_TYPE_HDR_METADATA_EXT;
    // Rec.2020 primaries and D65 white point. These values match the HDR10/PQ transform used by the final
    // presentation shaders, which maps scene highlights to a 1,000-nit mastering target.
    metadata.displayPrimaryRed = s_Hdr10DisplayPrimaryRed;
    metadata.displayPrimaryGreen = s_Hdr10DisplayPrimaryGreen;
    metadata.displayPrimaryBlue = s_Hdr10DisplayPrimaryBlue;
    metadata.whitePoint = s_Hdr10WhitePointD65;
    metadata.maxLuminance = s_Hdr10MasteringPeakLuminance;
    metadata.minLuminance = s_Hdr10MinimumLuminance;
    metadata.maxContentLightLevel = s_Hdr10MasteringPeakLuminance;
    metadata.maxFrameAverageLightLevel = s_Hdr10MaximumFrameAverageLightLevel;
    vkSetHdrMetadataEXT(device, s_Hdr10MetadataSwapChainCount, &swapChain, &metadata);
}

inline u64 GetDeviceLocalMemoryBytes(VkPhysicalDevice physicalDevice){
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

inline void PopulateAdapterInfo(VkPhysicalDevice physicalDevice, AdapterInfo& outAdapter){
    VkPhysicalDeviceProperties2 properties2 = {};
    properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    VkPhysicalDeviceIDProperties idProperties = {};
    idProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    properties2.pNext = &idProperties;
    vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);

    const auto& properties = properties2.properties;
    outAdapter.name = properties.deviceName;
    outAdapter.vendorID = properties.vendorID;
    outAdapter.deviceID = properties.deviceID;
    outAdapter.dedicatedVideoMemory = GetDeviceLocalMemoryBytes(physicalDevice);

    NWB_MEMCPY(outAdapter.uuid.data(), outAdapter.uuid.size(), idProperties.deviceUUID, outAdapter.uuid.size());
    outAdapter.hasUUID = true;
    outAdapter.luid = {};
    outAdapter.hasLUID = false;
    if(idProperties.deviceLUIDValid){
        NWB_MEMCPY(outAdapter.luid.data(), outAdapter.luid.size(), idProperties.deviceLUID, outAdapter.luid.size());
        outAdapter.hasLUID = true;
    }
}

inline u64 BytesToMiB(u64 bytes){
    return bytes / s_BytesPerMiB;
}

inline bool SupportsRequestedOptionalDeviceFeature(const OptionalDeviceFeatureSet& requested, const OptionalDeviceFeatureSet& supported, DeviceExtensionFeature::Enum feature){
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
    case DeviceExtensionFeature::RayTracingInvocationReorderExt:
        return SupportsRequestedValue(requested.rayTracingInvocationReorderExt.rayTracingInvocationReorder, supported.rayTracingInvocationReorderExt.rayTracingInvocationReorder);
    case DeviceExtensionFeature::RayTracingLinearSweptSpheres:
        return supported.rayTracingLinearSweptSpheres.spheres == VK_TRUE || supported.rayTracingLinearSweptSpheres.linearSweptSpheres == VK_TRUE;
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
    case DeviceExtensionFeature::DescriptorBuffer:
        return SupportsRequestedValue(requested.descriptorBuffer.descriptorBuffer, supported.descriptorBuffer.descriptorBuffer);
    case DeviceExtensionFeature::DeviceFault:
        return SupportsRequestedValue(requested.deviceFault.deviceFault, supported.deviceFault.deviceFault);
    case DeviceExtensionFeature::TextureCompressionAstcHdr:
        return SupportsRequestedValue(
            requested.textureCompressionAstcHdr.textureCompressionASTC_HDR,
            supported.textureCompressionAstcHdr.textureCompressionASTC_HDR
        );
    case DeviceExtensionFeature::None:
    case DeviceExtensionFeature::Count:
    default:
        return true;
    }
}

inline void FinalizeOptionalDeviceFeatureEnablement(OptionalDeviceFeatureSet& enabled, const OptionalDeviceFeatureSet& supported){
    enabled.meshShader.taskShader = supported.meshShader.taskShader;
    enabled.rayTracingLinearSweptSpheres.spheres = supported.rayTracingLinearSweptSpheres.spheres;
    enabled.rayTracingLinearSweptSpheres.linearSweptSpheres = supported.rayTracingLinearSweptSpheres.linearSweptSpheres;
    enabled.deviceFault.deviceFaultVendorBinary = supported.deviceFault.deviceFaultVendorBinary;
}

inline void AppendFeatureStruct(void*& pNext, void* feature){
    reinterpret_cast<VkBaseOutStructure*>(feature)->pNext = reinterpret_cast<VkBaseOutStructure*>(pNext);
    pNext = feature;
}

inline void AppendOptionalDeviceFeature(void*& pNext, OptionalDeviceFeatureSet& features, DeviceExtensionFeature::Enum feature, bool* appended){
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

[[maybe_unused]] inline const char* DebugUtilsSeverityToString(const VkDebugUtilsMessageSeverityFlagBitsEXT severity){
    switch(severity){
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: return "verbose";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: return "info";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: return "warning";
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: return "error";
    default: return "unknown";
    }
}

inline VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT types,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* userData
){
    const i32 messageId = callbackData ? callbackData->messageIdNumber : 0;
    const auto* backend = static_cast<const BackendContext*>(userData);
    if(backend && backend->isValidationMessageIdIgnored(messageId))
        return VK_FALSE;

    const char* messageIdName = callbackData && callbackData->pMessageIdName ? callbackData->pMessageIdName : "";
    const char* message = callbackData && callbackData->pMessage ? callbackData->pMessage : "";
    NWB_LOGGER_WARNING(
        NWB_TEXT("Vulkan debug: [severity={} types=0x{:x} id={} name='{}'] {}"),
        StringConvert(DebugUtilsSeverityToString(severity)),
        static_cast<u32>(types),
        messageId,
        StringConvert(messageIdName),
        StringConvert(message)
    );

    return VK_FALSE;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

