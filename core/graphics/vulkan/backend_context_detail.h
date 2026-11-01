// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend_context.h"
#include "arena_names.h"
#include "aftermath.h"
#include "swap_chain_presentation.h"

#include <core/common/log.h>

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

static constexpr u64 s_BytesPerMiB = 1024ull * 1024ull;
// HDR10 metadata mirrors the Rec.2020/ST.2084 transform.
// Keep values named so a policy change cannot leave the advertised Vulkan metadata behind.
static constexpr VkXYColorEXT s_Hdr10DisplayPrimaryRed = { 0.708f, 0.292f };
static constexpr VkXYColorEXT s_Hdr10DisplayPrimaryGreen = { 0.170f, 0.797f };
static constexpr VkXYColorEXT s_Hdr10DisplayPrimaryBlue = { 0.131f, 0.046f };
static constexpr VkXYColorEXT s_Hdr10WhitePointD65 = { 0.3127f, 0.3290f };
static constexpr f32 s_Hdr10MasteringPeakLuminance = 1000.0f;
static constexpr f32 s_Hdr10MinimumLuminance = 0.005f;
static constexpr f32 s_Hdr10MaximumFrameAverageLightLevel = 400.0f;
static constexpr u32 s_Hdr10MetadataSwapChainCount = 1u;

inline constexpr StringView s_UnknownDebugLabel = "unknown";
inline constexpr StringView s_DisabledQueueReason = "disabled";
inline constexpr AStringView s_ValidationLayerName = "VK_LAYER_KHRONOS_validation";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Transfer is guaranteed for Graphics/Compute families, but Graphics does not imply Compute.
// Keep the registry faithful to family flags so a cross-family Graphics queue never receives Compute work by inheritance.
[[nodiscard]] inline constexpr GpuQueueCapability::Mask QueueCapabilitiesForQueueFlags(const VkQueueFlags queueFlags)noexcept{
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

template<typename T>
static T MakeVkFeatureStruct(VkStructureType sType)noexcept(noexcept(T{}) && noexcept(static_cast<T*>(nullptr)->sType = sType) && noexcept(static_cast<T*>(nullptr)->pNext = nullptr) && IsNothrowMoveConstructible_V<T> && IsNothrowDestructible_V<T>){
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

[[nodiscard]] OptionalDeviceFeatureSet MakeRequestedOptionalDeviceFeatures()noexcept;
[[nodiscard]] void* GetOptionalDeviceFeatureStruct(OptionalDeviceFeatureSet& features, DeviceExtensionFeature::Enum feature)noexcept;
[[nodiscard]] Format::Enum GetBackBufferFormat(const DeviceCreationParameters& params)noexcept;

[[nodiscard]] inline constexpr b8 SupportsRequestedValue(VkBool32 requested, VkBool32 supported)noexcept{
    return requested != VK_TRUE || supported == VK_TRUE;
}

[[nodiscard]] inline constexpr StringView BoolToYesNoText(const b8 value)noexcept{
    return value ? StringView("yes") : StringView("no");
}

[[nodiscard]] ScratchString VulkanVersionToString(Alloc::ScratchArena& arena, u32 version);

[[nodiscard]] AStringView PhysicalDeviceTypeToString(VkPhysicalDeviceType type)noexcept;
[[nodiscard]] AStringView SwapChainFormatToString(VkFormat format)noexcept;
[[nodiscard]] AStringView ColorSpaceToString(VkColorSpaceKHR colorSpace)noexcept;
[[nodiscard]] AStringView PresentModeToString(VkPresentModeKHR mode)noexcept;
void SetHdr10Metadata(const VolkDeviceTable& deviceDispatch, VkDevice device, VkSwapchainKHR swapChain);
[[nodiscard]] u64 GetDeviceLocalMemoryBytes(const VolkInstanceTable& instanceDispatch, VkPhysicalDevice physicalDevice);
void PopulateAdapterInfo(const VolkInstanceTable& instanceDispatch, VkPhysicalDevice physicalDevice, AdapterInfo& outAdapter);

[[nodiscard]] inline constexpr u64 BytesToMiB(u64 bytes)noexcept{
    return bytes / s_BytesPerMiB;
}

[[nodiscard]] b8 SupportsRequestedOptionalDeviceFeature(const OptionalDeviceFeatureSet& requested, const OptionalDeviceFeatureSet& supported, DeviceExtensionFeature::Enum feature)noexcept;
void FinalizeOptionalDeviceFeatureEnablement(OptionalDeviceFeatureSet& enabled, const OptionalDeviceFeatureSet& supported)noexcept;

template<typename FeatureT>
inline void AppendFeatureStruct(void*& pNext, FeatureT& feature)noexcept{
    reinterpret_cast<VkBaseOutStructure&>(feature).pNext = static_cast<VkBaseOutStructure*>(pNext);
    pNext = &feature;
}

void AppendOptionalDeviceFeature(DeviceExtensionFeature::Enum feature, void*& pNext, OptionalDeviceFeatureSet& features, Array<b8, static_cast<usize>(DeviceExtensionFeature::Count)>& appended)noexcept;
[[nodiscard]] AStringView DebugUtilsSeverityToString(VkDebugUtilsMessageSeverityFlagBitsEXT severity)noexcept;
VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types, const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

