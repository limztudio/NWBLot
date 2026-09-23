// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"

#include <core/task/cpu/scheduler.h>
#include "command_buffer_resource_references.h"
#include "heap_binding_contract.h"
#include "host_readback_sync.h"
#include "native_buffer_provenance.h"
#include "native_queue_state.h"
#include "native_texture_provenance.h"
#include "submitted_command_buffer_owner_lookup.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Device;
class VulkanTestDispatchAccess;
class Queue;
class TrackedCommandBuffer;
class StateTracker;
class GpuDescriptorHeap;
class DescriptorBufferManager;

class Buffer;
class Texture;
class AccelStruct;
class OpacityMicromap;

struct VulkanContext;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Core Vulkan objects and capabilities


struct VulkanContext{
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    VolkInstanceTable instanceDispatch = {};
    VolkDeviceTable deviceDispatch = {};
    VkAllocationCallbacks* allocationCallbacks = nullptr;
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;

    Alloc::GlobalArena& objectArena;
    GraphicsAllocator& allocator;
    CpuTaskScheduler& cpuScheduler;

    VkPhysicalDeviceProperties physicalDeviceProperties{};
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties{};

    VkPhysicalDeviceAccelerationStructurePropertiesKHR accelStructProperties{};
    // These are the feature bits actually enabled through vkCreateDevice's pNext chain.  Extension names alone
    // are not capability answers because an enabled extension may have had its feature bit disabled.
    bool accelerationStructureFeatureEnabled = false;
    bool rayTracingPipelineFeatureEnabled = false;
    bool rayQueryFeatureEnabled = false;
    bool opacityMicromapFeatureEnabled = false;
    bool clusterAccelerationStructureFeatureEnabled = false;
    bool rayTracingInvocationReorderFeatureEnabled = false;
    bool rayTracingInvocationReorderExtFeatureEnabled = false;
    // True only after an enabled calibrated-timestamp extension exposes the DEVICE domain and its dispatch pair
    // completes a device-domain probe. It is immutable after Device construction.
    bool comparableGpuTimestamps = false;
    bool hostQueryResetFeatureEnabled = false;
    bool textureCompressionBcFeatureEnabled = false;
    bool textureCompressionAstcLdrFeatureEnabled = false;
    bool textureCompressionAstcHdrFeatureEnabled = false;
    bool independentBlendFeatureEnabled = false;
    bool fullDrawIndexUint32FeatureEnabled = false;
    bool multiDrawIndirectFeatureEnabled = false;
    bool drawIndirectFirstInstanceFeatureEnabled = false;
    // Descriptor-buffer limits used for layout and offsets.
    VkPhysicalDeviceDescriptorBufferPropertiesEXT descriptorBufferProperties{};
    VkPhysicalDeviceCooperativeVectorPropertiesNV coopVecProperties{};
    // Cooperative-vector feature bits from the device-create chain, not a later probe.
    VkPhysicalDeviceCooperativeVectorFeaturesNV coopVecFeatures{};
    VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures{};
    VkPhysicalDeviceMeshShaderPropertiesEXT meshShaderProperties{};
    VkPhysicalDeviceRayTracingLinearSweptSpheresFeaturesNV rayTracingLinearSweptSpheresFeatures{};
    VkPhysicalDeviceClusterAccelerationStructurePropertiesNV nvClusterAccelerationStructureProperties{};
    // Core subgroup properties (the engine requires Vulkan 1.3).
    VkPhysicalDeviceSubgroupProperties subgroupProperties{};
    DescriptorBufferManager* descriptorBufferManager = nullptr;

    // Physical families used for Graphics/AsyncCompute/Transfer resource sharing. Every auxiliary family is
    // invalid unless the optional cross-family same-class route registered that physical transport on this Device.
    i32 graphicsQueueFamilyIndex = s_InvalidQueueFamilyIndex;
    i32 auxiliaryGraphicsQueueFamilyIndex = s_InvalidQueueFamilyIndex;
    i32 asyncComputeQueueFamilyIndex = s_InvalidQueueFamilyIndex;
    i32 auxiliaryAsyncComputeQueueFamilyIndex = s_InvalidQueueFamilyIndex;
    i32 transferQueueFamilyIndex = s_InvalidQueueFamilyIndex;
    i32 auxiliaryTransferQueueFamilyIndex = s_InvalidQueueFamilyIndex;

    struct Extensions{
        bool KHR_synchronization2 = false;
        bool KHR_calibrated_timestamps = false;
        bool KHR_ray_tracing_pipeline = false;
        bool KHR_ray_query = false;
        bool KHR_acceleration_structure = false;
        bool buffer_device_address = false;
        bool EXT_descriptor_buffer = false;
        bool EXT_calibrated_timestamps = false;
        bool EXT_debug_utils = false;
        bool KHR_swapchain = false;
        bool KHR_dynamic_rendering = false;
        bool EXT_opacity_micromap = false;
        bool NV_cooperative_vector = false;
        bool NV_cluster_acceleration_structure = false;
        bool NV_device_diagnostic_checkpoints = false;
        bool EXT_device_fault = false;
        bool EXT_texture_compression_astc_hdr = false;
        bool AMD_buffer_marker = false;
        bool EXT_mesh_shader = false;
        bool KHR_fragment_shading_rate = false;
        bool EXT_ray_tracing_invocation_reorder = false;
        bool NV_ray_tracing_invocation_reorder = false;
        bool NV_ray_tracing_linear_swept_spheres = false;
    } extensions;
    u16 deviceGeneration = 0u;
    bool asyncComputeLaneEnabled = false;
    bool transferQueueEnabled = false;


    explicit VulkanContext(GraphicsAllocator& allocatorRef, CpuTaskScheduler& cpuSchedulerRef, u16 generation = 0u)
        : objectArena(allocatorRef.getObjectArena())
        , allocator(allocatorRef)
        , cpuScheduler(cpuSchedulerRef)
        , deviceGeneration(generation)
    {}
    VulkanContext(
        GraphicsAllocator& allocatorRef,
        CpuTaskScheduler& cpuSchedulerRef,
        VkInstance inst,
        VkPhysicalDevice physDev,
        VkDevice dev,
        PFN_vkGetInstanceProcAddr getInstanceProcAddress,
        const VolkInstanceTable& instanceDispatchTable,
        const VolkDeviceTable& deviceDispatchTable,
        VkAllocationCallbacks* allocCb,
        u16 generation = 0u)
        : instance(inst)
        , physicalDevice(physDev)
        , device(dev)
        , getInstanceProcAddr(getInstanceProcAddress)
        , instanceDispatch(instanceDispatchTable)
        , deviceDispatch(deviceDispatchTable)
        , allocationCallbacks(allocCb)
        , objectArena(allocatorRef.getObjectArena())
        , allocator(allocatorRef)
        , cpuScheduler(cpuSchedulerRef)
        , deviceGeneration(generation)
    {}
};

struct QueueFamilySharingInfo{
    VkSharingMode mode = VK_SHARING_MODE_EXCLUSIVE;
    Array<u32, 6u> familyIndices = {};
    u32 familyIndexCount = 0u;

    [[nodiscard]] const u32* data()const{ return familyIndexCount > 0u ? familyIndices.data() : nullptr; }
};

inline QueueFamilySharingInfo ResolveQueueFamilySharing(
    const ResourceQueueSharing::Mask sharing,
    const VulkanContext& context
){
    QueueFamilySharingInfo result;
    const u8 sharingBits = static_cast<u8>(sharing);
    const auto appendFamily = [&result](const i32 familyIndex){
        if(familyIndex == s_InvalidQueueFamilyIndex)
            return;
        for(u32 index = 0u; index < result.familyIndexCount; ++index){
            if(result.familyIndices[index] == static_cast<u32>(familyIndex))
                return;
        }
        NWB_ASSERT(result.familyIndexCount < result.familyIndices.size());
        result.familyIndices[result.familyIndexCount] = static_cast<u32>(familyIndex);
        ++result.familyIndexCount;
    };

    if(sharingBits & static_cast<u8>(ResourceQueueSharing::Graphics)){
        appendFamily(context.graphicsQueueFamilyIndex);
        appendFamily(context.auxiliaryGraphicsQueueFamilyIndex);
    }
    if(
        (sharingBits & static_cast<u8>(ResourceQueueSharing::AsyncCompute))
        && context.asyncComputeLaneEnabled
    ){
        appendFamily(context.asyncComputeQueueFamilyIndex);
        appendFamily(context.auxiliaryAsyncComputeQueueFamilyIndex);
    }
    if(
        (sharingBits & static_cast<u8>(ResourceQueueSharing::Transfer))
        && context.transferQueueEnabled
    ){
        appendFamily(context.transferQueueFamilyIndex);
        appendFamily(context.auxiliaryTransferQueueFamilyIndex);
    }

    if(result.familyIndexCount < 2u){
        result.familyIndexCount = 0u;
        return result;
    }

    result.mode = VK_SHARING_MODE_CONCURRENT;
    return result;
}

inline bool UsesConcurrentQueueSharing(
    const ResourceQueueSharing::Mask sharing,
    const VulkanContext& context
){
    return ResolveQueueFamilySharing(sharing, context).mode == VK_SHARING_MODE_CONCURRENT;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

