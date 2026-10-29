// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"
#include "backend_forward.h"

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
    // These are the feature bits actually enabled through vkCreateDevice's pNext chain.
    // Extension names alone are not capability answers because an enabled extension may have had its feature bit disabled.
    b8 accelerationStructureFeatureEnabled = false;
    b8 rayTracingPipelineFeatureEnabled = false;
    b8 rayQueryFeatureEnabled = false;
    b8 opacityMicromapFeatureEnabled = false;
    b8 clusterAccelerationStructureFeatureEnabled = false;
    b8 rayTracingInvocationReorderFeatureEnabled = false;
    b8 rayTracingInvocationReorderExtFeatureEnabled = false;
    // True only after an enabled calibrated-timestamp extension exposes the DEVICE domain and its dispatch pair completes a device-domain probe.
    // It is immutable after Device construction.
    b8 comparableGpuTimestamps = false;
    b8 hostQueryResetFeatureEnabled = false;
    b8 textureCompressionBcFeatureEnabled = false;
    b8 textureCompressionAstcLdrFeatureEnabled = false;
    b8 textureCompressionAstcHdrFeatureEnabled = false;
    b8 independentBlendFeatureEnabled = false;
    b8 fullDrawIndexUint32FeatureEnabled = false;
    b8 multiDrawIndirectFeatureEnabled = false;
    b8 drawIndirectFirstInstanceFeatureEnabled = false;
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

    // Physical families used for Graphics/AsyncCompute/Transfer resource sharing.
    // Every auxiliary family is invalid unless the optional cross-family same-class route registered that physical transport on this Device.
    i32 graphicsQueueFamilyIndex = s_InvalidQueueFamilyIndex;
    i32 auxiliaryGraphicsQueueFamilyIndex = s_InvalidQueueFamilyIndex;
    i32 asyncComputeQueueFamilyIndex = s_InvalidQueueFamilyIndex;
    i32 auxiliaryAsyncComputeQueueFamilyIndex = s_InvalidQueueFamilyIndex;
    i32 transferQueueFamilyIndex = s_InvalidQueueFamilyIndex;
    i32 auxiliaryTransferQueueFamilyIndex = s_InvalidQueueFamilyIndex;

    struct Extensions{
        b8 khrSynchronization2 = false;
        b8 khrCalibratedTimestamps = false;
        b8 khrRayTracingPipeline = false;
        b8 khrRayQuery = false;
        b8 khrAccelerationStructure = false;
        b8 bufferDeviceAddress = false;
        b8 extDescriptorBuffer = false;
        b8 extCalibratedTimestamps = false;
        b8 extDebugUtils = false;
        b8 khrSwapchain = false;
        b8 khrDynamicRendering = false;
        b8 extOpacityMicromap = false;
        b8 nvCooperativeVector = false;
        b8 nvClusterAccelerationStructure = false;
        b8 nvDeviceDiagnosticCheckpoints = false;
        b8 extDeviceFault = false;
        b8 extTextureCompressionAstcHdr = false;
        b8 amdBufferMarker = false;
        b8 extMeshShader = false;
        b8 khrFragmentShadingRate = false;
        b8 extRayTracingInvocationReorder = false;
        b8 nvRayTracingInvocationReorder = false;
        b8 nvRayTracingLinearSweptSpheres = false;
    } extensions;
    u16 deviceGeneration = 0u;
    b8 asyncComputeLaneEnabled = false;
    b8 transferQueueEnabled = false;


public:
    explicit VulkanContext(GraphicsAllocator& allocatorRef, CpuTaskScheduler& cpuSchedulerRef, u16 generation = 0u)noexcept
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
        u16 generation = 0u
    )noexcept
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

inline constexpr u32 s_MaxSharedQueueFamilyCount = 6u;
inline constexpr u32 s_MinConcurrentSharingQueueFamilyCount = 2u;

struct QueueFamilySharingInfo{
    VkSharingMode mode = VK_SHARING_MODE_EXCLUSIVE;
    Array<u32, s_MaxSharedQueueFamilyCount> familyIndices = {};
    u32 familyIndexCount = 0u;


public:
    [[nodiscard]] const u32* data()const noexcept{ return familyIndexCount > 0u ? familyIndices.data() : nullptr; }
};

inline QueueFamilySharingInfo ResolveQueueFamilySharing(const ResourceQueueSharing::Mask sharing, const VulkanContext& context)noexcept{
    QueueFamilySharingInfo result;
    const u8 sharingBits = static_cast<u8>(sharing);
    const auto appendFamily = [&result](const i32 familyIndex)noexcept{
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
    if((sharingBits & static_cast<u8>(ResourceQueueSharing::AsyncCompute)) && context.asyncComputeLaneEnabled){
        appendFamily(context.asyncComputeQueueFamilyIndex);
        appendFamily(context.auxiliaryAsyncComputeQueueFamilyIndex);
    }
    if((sharingBits & static_cast<u8>(ResourceQueueSharing::Transfer)) && context.transferQueueEnabled){
        appendFamily(context.transferQueueFamilyIndex);
        appendFamily(context.auxiliaryTransferQueueFamilyIndex);
    }

    if(result.familyIndexCount < s_MinConcurrentSharingQueueFamilyCount){
        result.familyIndexCount = 0u;
        return result;
    }

    result.mode = VK_SHARING_MODE_CONCURRENT;
    return result;
}

inline b8 UsesConcurrentQueueSharing(const ResourceQueueSharing::Mask sharing, const VulkanContext& context)noexcept{
    return ResolveQueueFamilySharing(sharing, context).mode == VK_SHARING_MODE_CONCURRENT;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

