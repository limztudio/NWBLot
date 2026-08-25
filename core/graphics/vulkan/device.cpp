// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"
#include "device_detail.h"

#include <core/common/log.h>
#include <global/atomic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan_device{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Queue timeline values are only meaningful within one logical-device lifetime. Physical queue indices are assigned
// by the Device registry (not CommandQueue ordinals) and the generation makes a recreated Device reject old tokens.
static Atomic<u32> s_NextDeviceGeneration{ 1u };

[[nodiscard]] static u16 AllocateDeviceGeneration()noexcept{
    u16 generation = static_cast<u16>(s_NextDeviceGeneration.fetch_add(1u, MemoryOrder::relaxed));
    while(generation == 0u)
        generation = static_cast<u16>(s_NextDeviceGeneration.fetch_add(1u, MemoryOrder::relaxed));
    return generation;
}

static constexpr u32 s_CalibratedTimestampProbeAttemptCount = 4u;

template<typename EnumerateTimeDomains, typename GetCalibratedTimestamps>
[[nodiscard]] static bool ProbeComparableGpuTimestamps(
    const VkPhysicalDevice physicalDevice,
    const VkDevice device,
    EnumerateTimeDomains enumerateTimeDomains,
    GetCalibratedTimestamps getCalibratedTimestamps,
    Alloc::ScratchArena& scratchArena
){
    if(!enumerateTimeDomains || !getCalibratedTimestamps){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Calibrated timestamp entry points are unavailable."));
        return false;
    }

    for(u32 attempt = 0u; attempt < s_CalibratedTimestampProbeAttemptCount; ++attempt){
        u32 timeDomainCount = 0u;
        const VkResult countResult = enumerateTimeDomains(physicalDevice, &timeDomainCount, nullptr);
        if(countResult == VK_INCOMPLETE)
            continue;
        if(countResult != VK_SUCCESS){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to enumerate calibrated timestamp domains. {}"), ResultToString(countResult));
            return false;
        }
        if(timeDomainCount == 0u)
            return false;

        Vector<VkTimeDomainKHR, Alloc::ScratchArena> timeDomains(timeDomainCount, scratchArena);
        u32 writtenTimeDomainCount = timeDomainCount;
        const VkResult domainsResult = enumerateTimeDomains(physicalDevice, &writtenTimeDomainCount, timeDomains.data());
        if(domainsResult == VK_INCOMPLETE)
            continue;
        if(domainsResult != VK_SUCCESS){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to read calibrated timestamp domains. {}"), ResultToString(domainsResult));
            return false;
        }
        if(writtenTimeDomainCount > timeDomains.size()){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Calibrated timestamp domain enumeration returned an invalid count."));
            return false;
        }

        bool hasDeviceTimeDomain = false;
        for(u32 domainIndex = 0u; domainIndex < writtenTimeDomainCount; ++domainIndex){
            if(timeDomains[domainIndex] == VK_TIME_DOMAIN_DEVICE_KHR){
                hasDeviceTimeDomain = true;
                break;
            }
        }
        if(!hasDeviceTimeDomain)
            return false;

        VkCalibratedTimestampInfoKHR timestampInfo{};
        timestampInfo.sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR;
        timestampInfo.timeDomain = VK_TIME_DOMAIN_DEVICE_KHR;
        u64 timestamp = 0u;
        u64 maxDeviation = 0u;
        const VkResult timestampResult = getCalibratedTimestamps(device, 1u, &timestampInfo, &timestamp, &maxDeviation);
        if(timestampResult != VK_SUCCESS){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to probe the calibrated device timestamp domain. {}"), ResultToString(timestampResult));
            return false;
        }
        return true;
    }

    NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Calibrated timestamp domain enumeration did not stabilize."));
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Device::Device(const DeviceDesc& desc)
    : RefCounter<GraphicsResource>(desc.threadPool)
    , m_gpuCrashDiagnosticsEnabled(desc.gpuCrashDiagnosticsEnabled)
    , m_deviceGeneration(__hidden_vulkan_device::AllocateDeviceGeneration())
    , m_gpuCrashTracker(desc.allocator.getObjectArena())
    , m_gpuCrashReportArena(VulkanArenaScope::s_GpuCrashReportArena, Alloc::PersistentArena::StructureAlignedSize(s_GpuCrashReportArenaSize))
    , m_gpuCrashVendorBinaryArena(VulkanArenaScope::s_GpuCrashVendorBinaryArena, Alloc::PersistentArena::StructureAlignedSize(s_MaxDeviceFaultVendorBinaryBytes))
    , m_amdBreadcrumb(desc.allocator.getObjectArena())
    , m_context(
        desc.allocator,
        desc.threadPool,
        desc.instance,
        desc.physicalDevice,
        desc.device,
        desc.allocationCallbacks,
        m_deviceGeneration
    )
    , m_allocator(m_context)
    , m_descriptorBufferManager(*this, m_context, m_allocator)
    , m_gpuDescriptorHeap(*this)
    , m_pipelineCacheDirectory(m_context.objectArena, desc.pipelineCacheDirectory)
    , m_pipelineCacheVolumeName(m_context.objectArena)
    , m_physicalQueues(m_context.objectArena)
    , m_physicalQueueInfos(m_context.objectArena)
#if !defined(NWB_FINAL)
    , m_submissionWaitTokensForTesting(m_context.objectArena)
#endif
    , m_uploadManager(*this, s_DefaultUploadChunkSize, 0, false)
    , m_scratchManager(*this, s_DefaultScratchChunkSize, s_ScratchMemoryLimit, true)
{
    VkResult res = VK_SUCCESS;

    m_context.descriptorBufferManager = &m_descriptorBufferManager;
    if(desc.physicalQueues && desc.physicalQueueCount != 0u){
        for(usize queueIndex = 0u; queueIndex < desc.physicalQueueCount; ++queueIndex){
            if(!registerPhysicalQueue(desc.physicalQueues[queueIndex]))
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to register a native physical queue."));
        }
    }
    else{
        // Preserve construction compatibility for older callers while assigning registry IDs independently from
        // CommandQueue ordinals. The grouped fields carry family identity, so retain a Graphics+Compute legacy
        // fallback when that real family supports it instead of degrading every old caller to Graphics-only.
        u32 legacyQueueFamilyCount = 0u;
        if(m_context.physicalDevice != VK_NULL_HANDLE)
            vkGetPhysicalDeviceQueueFamilyProperties(m_context.physicalDevice, &legacyQueueFamilyCount, nullptr);
        Alloc::ScratchArena legacyQueueArena(VulkanArenaScope::s_QueueFamilyQueryArena);
        Vector<VkQueueFamilyProperties, Alloc::ScratchArena> legacyQueueFamilies(legacyQueueFamilyCount, legacyQueueArena);
        if(!legacyQueueFamilies.empty()){
            vkGetPhysicalDeviceQueueFamilyProperties(
                m_context.physicalDevice,
                &legacyQueueFamilyCount,
                legacyQueueFamilies.data()
            );
        }
        const auto capabilitiesForLegacyQueue = [&legacyQueueFamilies](
            const i32 queueFamily,
            const CommandQueue::Enum queueClass
        ){
            if(
                queueFamily < 0
                || static_cast<usize>(queueFamily) >= legacyQueueFamilies.size()
            )
                return VulkanDetail::DeviceMinimumQueueCapabilities(queueClass);
            return VulkanDetail::DeviceQueueCapabilitiesForQueueFlags(
                legacyQueueFamilies[static_cast<usize>(queueFamily)].queueFlags
            );
        };
        const auto timestampValidBitsForLegacyQueue = [&legacyQueueFamilies](const i32 queueFamily){
            if(queueFamily < 0 || static_cast<usize>(queueFamily) >= legacyQueueFamilies.size())
                return 0u;
            return legacyQueueFamilies[static_cast<usize>(queueFamily)].timestampValidBits;
        };
        const VulkanPhysicalQueueDesc legacyQueues[] = {
            VulkanPhysicalQueueDesc{
                .queue = desc.graphicsQueue,
                .queueClass = CommandQueue::Graphics,
                .capabilities = capabilitiesForLegacyQueue(desc.graphicsQueueIndex, CommandQueue::Graphics),
                .familyIndex = desc.graphicsQueueIndex >= 0
                    ? static_cast<u32>(desc.graphicsQueueIndex)
                    : Limit<u32>::s_Max,
                .queueIndex = s_GraphicsQueueIndex,
                .timestampValidBits = timestampValidBitsForLegacyQueue(desc.graphicsQueueIndex),
                .dedicated = false,
                .primaryForClass = true,
            },
            VulkanPhysicalQueueDesc{
                .queue = desc.computeQueue,
                .queueClass = CommandQueue::Compute,
                .capabilities = capabilitiesForLegacyQueue(desc.computeQueueIndex, CommandQueue::Compute),
                .familyIndex = desc.computeQueueIndex >= 0
                    ? static_cast<u32>(desc.computeQueueIndex)
                    : Limit<u32>::s_Max,
                .queueIndex = s_ComputeQueueIndex,
                .timestampValidBits = timestampValidBitsForLegacyQueue(desc.computeQueueIndex),
                .dedicated = desc.asyncComputeLaneEnabled,
                .primaryForClass = true,
            },
            VulkanPhysicalQueueDesc{
                .queue = desc.transferQueue,
                .queueClass = CommandQueue::Transfer,
                .capabilities = capabilitiesForLegacyQueue(desc.transferQueueIndex, CommandQueue::Transfer),
                .familyIndex = desc.transferQueueIndex >= 0
                    ? static_cast<u32>(desc.transferQueueIndex)
                    : Limit<u32>::s_Max,
                .queueIndex = s_TransferQueueIndex,
                .timestampValidBits = timestampValidBitsForLegacyQueue(desc.transferQueueIndex),
                .dedicated = desc.transferQueueEnabled,
                .primaryForClass = true,
            },
        };
        for(const VulkanPhysicalQueueDesc& queue : legacyQueues){
            if(
                queue.queue != VK_NULL_HANDLE
                && queue.familyIndex != Limit<u32>::s_Max
                && !registerPhysicalQueue(queue)
            )
                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to register a legacy physical queue."));
        }
    }
    configureLegacyQueueContext();

    vkGetPhysicalDeviceProperties(m_context.physicalDevice, &m_context.physicalDeviceProperties);
    vkGetPhysicalDeviceMemoryProperties(m_context.physicalDevice, &m_context.memoryProperties);
    m_pipelineCacheVolumeName.assign(VulkanDetail::s_PipelineCacheVolumeName);

    m_context.extensions.buffer_device_address = desc.bufferDeviceAddressSupported;
    m_context.extensions.KHR_dynamic_rendering = desc.dynamicRenderingSupported;
    m_context.extensions.KHR_synchronization2 = desc.synchronization2Supported;
    m_context.independentBlendFeatureEnabled = desc.independentBlendFeatureEnabled;
    m_context.fullDrawIndexUint32FeatureEnabled = desc.fullDrawIndexUint32FeatureEnabled;
    m_context.multiDrawIndirectFeatureEnabled = desc.multiDrawIndirectFeatureEnabled;
    m_context.drawIndirectFirstInstanceFeatureEnabled = desc.drawIndirectFirstInstanceFeatureEnabled;
    m_context.accelerationStructureFeatureEnabled = desc.accelerationStructureFeatureEnabled;
    m_context.rayTracingPipelineFeatureEnabled = desc.rayTracingPipelineFeatureEnabled;
    m_context.rayQueryFeatureEnabled = desc.rayQueryFeatureEnabled;
    m_context.opacityMicromapFeatureEnabled = desc.opacityMicromapFeatureEnabled;
    m_context.clusterAccelerationStructureFeatureEnabled = desc.clusterAccelerationStructureFeatureEnabled;
    m_context.rayTracingInvocationReorderFeatureEnabled = desc.rayTracingInvocationReorderFeatureEnabled;
    m_context.rayTracingInvocationReorderExtFeatureEnabled = desc.rayTracingInvocationReorderExtFeatureEnabled;

    for(usize i = 0; i < desc.numInstanceExtensions; ++i){
        const char* ext = desc.instanceExtensions[i];
        if(NWB_STRCMP(ext, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0)
            m_context.extensions.EXT_debug_utils = true;
    }

    for(usize i = 0; i < desc.numDeviceExtensions; ++i){
        const char* ext = desc.deviceExtensions[i];
        if(NWB_STRCMP(ext, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME) == 0)
            m_context.extensions.KHR_synchronization2 = true;
        else if(NWB_STRCMP(ext, VK_KHR_CALIBRATED_TIMESTAMPS_EXTENSION_NAME) == 0)
            m_context.extensions.KHR_calibrated_timestamps = true;
        else if(NWB_STRCMP(ext, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) == 0)
            m_context.extensions.KHR_ray_tracing_pipeline = true;
        else if(NWB_STRCMP(ext, VK_KHR_RAY_QUERY_EXTENSION_NAME) == 0)
            m_context.extensions.KHR_ray_query = true;
        else if(NWB_STRCMP(ext, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0)
            m_context.extensions.KHR_acceleration_structure = true;
        else if(NWB_STRCMP(ext, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
            m_context.extensions.KHR_swapchain = true;
        else if(NWB_STRCMP(ext, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) == 0)
            m_context.extensions.KHR_dynamic_rendering = true;
        else if(NWB_STRCMP(ext, VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME) == 0)
            m_context.extensions.EXT_descriptor_buffer = true;
        else if(NWB_STRCMP(ext, VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME) == 0)
            m_context.extensions.EXT_calibrated_timestamps = true;
        else if(NWB_STRCMP(ext, VK_EXT_OPACITY_MICROMAP_EXTENSION_NAME) == 0)
            m_context.extensions.EXT_opacity_micromap = true;
        else if(NWB_STRCMP(ext, VK_NV_COOPERATIVE_VECTOR_EXTENSION_NAME) == 0)
            m_context.extensions.NV_cooperative_vector = true;
        else if(NWB_STRCMP(ext, VK_NV_CLUSTER_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0)
            m_context.extensions.NV_cluster_acceleration_structure = true;
        else if(NWB_STRCMP(ext, VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME) == 0)
            m_context.extensions.NV_device_diagnostic_checkpoints = true;
        else if(NWB_STRCMP(ext, VK_EXT_DEVICE_FAULT_EXTENSION_NAME) == 0)
            m_context.extensions.EXT_device_fault = true;
        else if(NWB_STRCMP(ext, VK_EXT_TEXTURE_COMPRESSION_ASTC_HDR_EXTENSION_NAME) == 0)
            m_context.extensions.EXT_texture_compression_astc_hdr = true;
        else if(NWB_STRCMP(ext, VK_AMD_BUFFER_MARKER_EXTENSION_NAME) == 0)
            m_context.extensions.AMD_buffer_marker = true;
        else if(NWB_STRCMP(ext, VK_EXT_MESH_SHADER_EXTENSION_NAME) == 0)
            m_context.extensions.EXT_mesh_shader = true;
        else if(NWB_STRCMP(ext, VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME) == 0)
            m_context.extensions.KHR_fragment_shading_rate = true;
        else if(NWB_STRCMP(ext, VK_EXT_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME) == 0)
            m_context.extensions.EXT_ray_tracing_invocation_reorder = true;
        else if(NWB_STRCMP(ext, VK_NV_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME) == 0)
            m_context.extensions.NV_ray_tracing_invocation_reorder = true;
        else if(NWB_STRCMP(ext, VK_NV_RAY_TRACING_LINEAR_SWEPT_SPHERES_EXTENSION_NAME) == 0)
            m_context.extensions.NV_ray_tracing_linear_swept_spheres = true;
    }

    Alloc::ScratchArena calibratedTimestampProbeArena(VulkanArenaScope::s_DeviceExtensionSetupArena);
    bool comparableGpuTimestamps = false;
    if(m_context.extensions.KHR_calibrated_timestamps){
        comparableGpuTimestamps = __hidden_vulkan_device::ProbeComparableGpuTimestamps(
            m_context.physicalDevice,
            m_context.device,
            vkGetPhysicalDeviceCalibrateableTimeDomainsKHR,
            vkGetCalibratedTimestampsKHR,
            calibratedTimestampProbeArena
        );
    }
    if(!comparableGpuTimestamps && m_context.extensions.EXT_calibrated_timestamps){
        comparableGpuTimestamps = __hidden_vulkan_device::ProbeComparableGpuTimestamps(
            m_context.physicalDevice,
            m_context.device,
            vkGetPhysicalDeviceCalibrateableTimeDomainsEXT,
            vkGetCalibratedTimestampsEXT,
            calibratedTimestampProbeArena
        );
    }
    m_context.comparableGpuTimestamps = comparableGpuTimestamps;

    m_context.meshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
    if(m_context.extensions.EXT_mesh_shader){
        m_context.meshShaderFeatures.meshShader = desc.meshShaderFeatureEnabled ? VK_TRUE : VK_FALSE;
        m_context.meshShaderFeatures.taskShader = desc.meshTaskShaderSupported ? VK_TRUE : VK_FALSE;
    }

    m_context.rayTracingLinearSweptSpheresFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_LINEAR_SWEPT_SPHERES_FEATURES_NV;
    if(m_context.extensions.NV_ray_tracing_linear_swept_spheres){
        m_context.rayTracingLinearSweptSpheresFeatures.spheres = desc.rayTracingSpheresSupported ? VK_TRUE : VK_FALSE;
        m_context.rayTracingLinearSweptSpheresFeatures.linearSweptSpheres = desc.rayTracingLinearSweptSpheresSupported ? VK_TRUE : VK_FALSE;
    }

    m_context.coopVecFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_VECTOR_FEATURES_NV;
    if(m_context.extensions.NV_cooperative_vector){
        m_context.coopVecFeatures.cooperativeVector = desc.cooperativeVectorFeatureEnabled ? VK_TRUE : VK_FALSE;
        m_context.coopVecFeatures.cooperativeVectorTraining = desc.cooperativeVectorTrainingFeatureEnabled ? VK_TRUE : VK_FALSE;
    }

    if(m_context.extensions.EXT_debug_utils && (!vkCmdBeginDebugUtilsLabelEXT || !vkCmdEndDebugUtilsLabelEXT)){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Debug utils marker entry points are unavailable."));
        m_context.extensions.EXT_debug_utils = false;
    }

    if(m_context.extensions.NV_device_diagnostic_checkpoints && (!vkCmdSetCheckpointNV || !vkGetQueueCheckpointDataNV)){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Device diagnostic checkpoint entry points are unavailable."));
        m_context.extensions.NV_device_diagnostic_checkpoints = false;
    }

    if(m_context.extensions.EXT_device_fault && !vkGetDeviceFaultInfoEXT){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Device fault info entry point is unavailable."));
        m_context.extensions.EXT_device_fault = false;
    }

    if(m_context.extensions.AMD_buffer_marker && !vkCmdWriteBufferMarkerAMD){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Buffer marker entry point is unavailable."));
        m_context.extensions.AMD_buffer_marker = false;
    }

    if(
        m_gpuCrashDiagnosticsEnabled
        && !m_context.extensions.NV_device_diagnostic_checkpoints
        && !m_context.extensions.AMD_buffer_marker
        && !m_context.extensions.EXT_device_fault
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: GPU crash diagnostics requested but no supported backend (device diagnostic checkpoints / buffer markers / device fault) is available; minimal text GPU crash reports remain enabled."));
    }

    if(
        m_context.extensions.KHR_acceleration_structure
        && (
            !vkCreateAccelerationStructureKHR
            || !vkDestroyAccelerationStructureKHR
            || !vkGetAccelerationStructureBuildSizesKHR
            || !vkGetAccelerationStructureDeviceAddressKHR
            || !vkCmdBuildAccelerationStructuresKHR
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Acceleration structure entry points are unavailable."));
        m_context.extensions.KHR_acceleration_structure = false;
        m_context.accelerationStructureFeatureEnabled = false;
    }

    if(
        m_context.extensions.KHR_ray_tracing_pipeline
        && (
            !vkCreateRayTracingPipelinesKHR
            || !vkGetRayTracingShaderGroupHandlesKHR
            || !vkCmdTraceRaysKHR
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Ray tracing pipeline entry points are unavailable."));
        m_context.extensions.KHR_ray_tracing_pipeline = false;
        m_context.rayTracingPipelineFeatureEnabled = false;
    }

    if(
        m_context.extensions.EXT_opacity_micromap
        && (
            !vkCreateMicromapEXT
            || !vkDestroyMicromapEXT
            || !vkGetMicromapBuildSizesEXT
            || !vkCmdBuildMicromapsEXT
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Opacity micromap entry points are unavailable."));
        m_context.extensions.EXT_opacity_micromap = false;
        m_context.opacityMicromapFeatureEnabled = false;
    }

    if(
        m_context.extensions.NV_cluster_acceleration_structure
        && (
            !vkGetClusterAccelerationStructureBuildSizesNV
            || !vkCmdBuildClusterAccelerationStructureIndirectNV
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Cluster acceleration structure entry points are unavailable."));
        m_context.extensions.NV_cluster_acceleration_structure = false;
        m_context.clusterAccelerationStructureFeatureEnabled = false;
    }

    if(
        m_context.extensions.NV_cooperative_vector
        && (
            !vkGetPhysicalDeviceCooperativeVectorPropertiesNV
            || !vkConvertCooperativeVectorMatrixNV
            || !vkCmdConvertCooperativeVectorMatrixNV
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Cooperative vector entry points are unavailable."));
        m_context.extensions.NV_cooperative_vector = false;
        m_context.coopVecFeatures.cooperativeVector = VK_FALSE;
        m_context.coopVecFeatures.cooperativeVectorTraining = VK_FALSE;
    }

    if(m_context.extensions.EXT_mesh_shader && !vkCmdDrawMeshTasksEXT){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Mesh shader draw entry point is unavailable."));
        m_context.extensions.EXT_mesh_shader = false;
        m_context.meshShaderFeatures.meshShader = VK_FALSE;
        m_context.meshShaderFeatures.taskShader = VK_FALSE;
    }

    {
        auto props2 = VulkanDetail::MakeVkStruct<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
        void* pNext = nullptr;

        m_context.subgroupProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
        m_context.subgroupProperties.pNext = pNext;
        pNext = &m_context.subgroupProperties;

        if(m_context.extensions.KHR_ray_tracing_pipeline){
            m_context.rayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
            m_context.rayTracingPipelineProperties.pNext = pNext;
            pNext = &m_context.rayTracingPipelineProperties;
        }

        if(m_context.extensions.KHR_acceleration_structure){
            m_context.accelStructProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
            m_context.accelStructProperties.pNext = pNext;
            pNext = &m_context.accelStructProperties;
        }

        if(m_context.extensions.EXT_descriptor_buffer){
            m_context.descriptorBufferProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT;
            m_context.descriptorBufferProperties.pNext = pNext;
            pNext = &m_context.descriptorBufferProperties;
        }

        if(m_context.extensions.EXT_mesh_shader){
            m_context.meshShaderProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT;
            m_context.meshShaderProperties.pNext = pNext;
            pNext = &m_context.meshShaderProperties;
        }

        if(m_context.extensions.NV_cluster_acceleration_structure){
            m_context.nvClusterAccelerationStructureProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CLUSTER_ACCELERATION_STRUCTURE_PROPERTIES_NV;
            m_context.nvClusterAccelerationStructureProperties.pNext = pNext;
            pNext = &m_context.nvClusterAccelerationStructureProperties;
        }

        if(m_context.extensions.NV_cooperative_vector){
            m_context.coopVecProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_VECTOR_PROPERTIES_NV;
            m_context.coopVecProperties.pNext = pNext;
            pNext = &m_context.coopVecProperties;
        }

        if(pNext){
            props2.pNext = pNext;
            vkGetPhysicalDeviceProperties2(m_context.physicalDevice, &props2);
        }
    }

    // Resolve every ASTC and BC sampled-image path up front. Texture loading consults this
    // cache, so it can select ASTC, BC, or an uncompressed fallback before allocating data.
    probeCompressedTextureFormats();

    // Descriptor-buffer entry points are mandatory for this device.
    if(
        !m_context.extensions.EXT_descriptor_buffer
        || !vkGetDescriptorEXT
        || !vkGetDescriptorSetLayoutBindingOffsetEXT
        || !vkCmdBindDescriptorBuffersEXT
        || !vkCmdSetDescriptorBufferOffsetsEXT
    ){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Required descriptor-buffer entry points are unavailable."));
    }

    if(!m_allocator.initialize())
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to initialize VMA allocator"));

    if(m_gpuCrashDiagnosticsEnabled && m_context.extensions.AMD_buffer_marker){
        VulkanDetail::AmdBreadcrumbRingLayout breadcrumbLayout;
        if(!VulkanDetail::TryBuildAmdBreadcrumbRingLayout(
            getPhysicalQueueTopology(),
            s_MaxAmdBreadcrumbSlots,
            breadcrumbLayout
        )){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Invalid AMD breadcrumb ring layout; AMD GPU breadcrumbs disabled."));
            m_context.extensions.AMD_buffer_marker = false;
        }
        else{
            m_amdBreadcrumb.layout = breadcrumbLayout;
            m_amdBreadcrumb.slotRecords.resize(breadcrumbLayout.totalSlotCount, AmdBreadcrumbSlotRecord{});
            m_amdBreadcrumb.nextSerials.resize(breadcrumbLayout.physicalQueueCount, 0u);

            auto breadcrumbInfo = VulkanDetail::MakeVkStruct<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
            breadcrumbInfo.size = breadcrumbLayout.totalByteSize;
            breadcrumbInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            Alloc::ScratchArena breadcrumbQueueArena(VulkanArenaScope::s_QueueFamilyQueryArena);
            Vector<u32, Alloc::ScratchArena> breadcrumbQueueFamilies(breadcrumbQueueArena);
            VulkanDetail::CollectUniquePhysicalQueueFamilyIndices(
                getPhysicalQueueTopology(),
                breadcrumbQueueFamilies
            );
            breadcrumbInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if(breadcrumbQueueFamilies.size() > 1u){
                breadcrumbInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
                breadcrumbInfo.queueFamilyIndexCount = static_cast<u32>(breadcrumbQueueFamilies.size());
                breadcrumbInfo.pQueueFamilyIndices = breadcrumbQueueFamilies.data();
            }
            const VkResult breadcrumbRes = m_allocator.createHostMappedBuffer(
                m_amdBreadcrumb.buffer,
                m_amdBreadcrumb.allocation,
                m_amdBreadcrumb.mappedMemory,
                breadcrumbInfo
            );
            if(breadcrumbRes == VK_SUCCESS && m_amdBreadcrumb.mappedMemory){
                NWB_MEMSET(m_amdBreadcrumb.mappedMemory, 0, static_cast<usize>(breadcrumbInfo.size));
            }
            else{
                // Release an unusable allocation before disabling breadcrumbs.
                if(breadcrumbRes == VK_SUCCESS){
                    m_allocator.destroyHostMappedBuffer(
                        m_amdBreadcrumb.buffer,
                        m_amdBreadcrumb.allocation,
                        m_amdBreadcrumb.mappedMemory
                    );
                }
                NWB_LOGGER_WARNING(
                    NWB_TEXT("Vulkan: Failed to allocate AMD breadcrumb buffer ({}); breadcrumbs disabled."),
                    ResultToString(breadcrumbRes)
                );
                m_context.extensions.AMD_buffer_marker = false;
            }
        }
    }


    // Initialize required global descriptor-buffer segments.
    if(
        m_context.extensions.EXT_descriptor_buffer
        && vkGetDescriptorEXT
        && vkGetDescriptorSetLayoutBindingOffsetEXT
        && vkCmdBindDescriptorBuffersEXT
        && vkCmdSetDescriptorBufferOffsetsEXT
    ){
        if(!m_descriptorBufferManager.initialize()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Required descriptor-buffer manager initialization failed."));
        }
    }

    if(m_context.extensions.EXT_descriptor_buffer && m_descriptorBufferManager.isEnabled()){
        GpuDescriptorHeapDesc heapDesc;
        heapDesc.setBindlessHeapAbi(desc.bindlessHeapAbi);
        if(!m_gpuDescriptorHeap.initialize(heapDesc))
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Required global GpuDescriptorHeap initialization failed."));
    }
    else{
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Required global GpuDescriptorHeap is unavailable."));
    }

    GraphicsBytes pipelineCacheInitialData{m_context.objectArena};
    if(!loadPipelineCacheData(pipelineCacheInitialData))
        NWB_LOGGER_INFO(NWB_TEXT("Vulkan: No usable pipeline cache found; starting with an empty cache."));

    auto cacheInfo = VulkanDetail::MakeVkStruct<VkPipelineCacheCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO);
    if(!pipelineCacheInitialData.empty()){
        cacheInfo.initialDataSize = pipelineCacheInitialData.size();
        cacheInfo.pInitialData = pipelineCacheInitialData.data();
    }
    res = vkCreatePipelineCache(m_context.device, &cacheInfo, m_context.allocationCallbacks, &m_context.pipelineCache);
    if(res != VK_SUCCESS && !pipelineCacheInitialData.empty()){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to create pipeline cache from runtime volume '{}'. Retrying empty cache. {}")
            , StringConvert(m_pipelineCacheVolumeName)
            , ResultToString(res)
        );
        cacheInfo.initialDataSize = 0;
        cacheInfo.pInitialData = nullptr;
        res = vkCreatePipelineCache(m_context.device, &cacheInfo, m_context.allocationCallbacks, &m_context.pipelineCache);
    }
    if(res != VK_SUCCESS){
        m_context.pipelineCache = VK_NULL_HANDLE;
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to create pipeline cache. {}"), ResultToString(res));
    }

}
Device::~Device(){
    waitForIdle();

    m_uploadManager.clear();
    m_scratchManager.clear();

    m_gpuDescriptorHeap.shutdownForDeviceTeardown();
    m_descriptorBufferManager.shutdown();

    for(Queue* queue : m_physicalQueues){
        if(queue)
            DestroyArenaObject(m_context.objectArena, queue);
    }
    m_physicalQueues.clear();
    m_physicalQueueInfos.clear();

    if(m_amdBreadcrumb.allocation)
        m_allocator.destroyHostMappedBuffer(m_amdBreadcrumb.buffer, m_amdBreadcrumb.allocation, m_amdBreadcrumb.mappedMemory);

    if(m_context.emptyDescriptorBufferSetLayout){
        vkDestroyDescriptorSetLayout(m_context.device, m_context.emptyDescriptorBufferSetLayout, m_context.allocationCallbacks);
        m_context.emptyDescriptorBufferSetLayout = VK_NULL_HANDLE;
    }

    if(m_context.pipelineCache){
        savePipelineCacheData();
        vkDestroyPipelineCache(m_context.device, m_context.pipelineCache, m_context.allocationCallbacks);
        m_context.pipelineCache = VK_NULL_HANDLE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Device::waitForIdle(){
    if(isDeviceLost())
        return false;

    VkResult res = VK_SUCCESS;

    res = vkDeviceWaitIdle(m_context.device);
    if(res == VK_ERROR_DEVICE_LOST){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Device was lost during waitForIdle."));
        captureGpuCrash("wait idle");
        return false;
    }
    else if(res != VK_SUCCESS){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to wait for device idle. {}"), ResultToString(res));
        return false;
    }

    for(Queue* queue : m_physicalQueues){
        if(queue)
            queue->waitForIdle();
    }
    m_gpuDescriptorHeap.collectRetired();

    return true;
}

void Device::runGarbageCollection(){
    // Avoid extra queue queries after device loss.
    if(isDeviceLost())
        return;

    for(Queue* queue : m_physicalQueues){
        if(queue){
            ScopedLock lock(queue->m_mutex);
            queue->updateLastFinishedID();
            if(isDeviceLost())
                return;
            queue->collectCompletedCommandBuffers();
        }
    }
    m_gpuDescriptorHeap.collectRetired();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DeviceHandle CreateDevice(const DeviceDesc& desc){
    auto* device = NewArenaObject<Device>(desc.allocator.getObjectArena(), desc);
    return DeviceHandle(device, DeviceHandle::deleter_type(&desc.allocator.getObjectArena()), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

