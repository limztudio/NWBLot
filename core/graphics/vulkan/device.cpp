// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"
#include "aftermath.h"

#include <core/filesystem/volume_file_system.h>
#include <core/filesystem/volume_staging.h>
#include <global/filesystem/volume_naming.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_PipelineCacheVirtualPath = "vulkan/pipeline_cache.bin";
// Pipeline-cache validation rejects data from different GPUs or drivers.
static constexpr AStringView s_PipelineCacheVolumeName = "runtime_pipeline_cache";
static constexpr u64 s_PipelineCacheVolumeSegmentSize = 16ull * 1024ull * 1024ull;
static constexpr u64 s_PipelineCacheVolumeMetadataSize = 4ull * 1024ull;
static constexpr usize s_PipelineCacheDataMaxAttempts = 4;

static AStringView TrimGpuCrashText(const AStringView text){
    return AStringView(text.data(), Min(text.size(), s_MaxGpuCrashMarkerChars));
}

static AStringView TrimGpuCrashText(const char* const text){
    return text ? TrimGpuCrashText(AStringView(text)) : AStringView();
}

static const char* GpuCrashAvailabilityText(const bool available){
    return available ? "available" : "unavailable";
}


static bool MountPipelineCacheVolume(
    const Path& directory,
    const AStringView volumeName,
    const bool createIfMissing,
    Filesystem::VolumeUsage::Enum usage,
    Filesystem::VolumeFileSystem& outVolume
){
    Filesystem::VolumeMountDesc mountDesc(directory.arena());
    if(!mountDesc.volumeName.assign(volumeName))
        return false;
    mountDesc.mountDirectory = directory;
    mountDesc.createIfMissing = createIfMissing;
    mountDesc.usage = usage;
    if(createIfMissing){
        mountDesc.segmentSize = s_PipelineCacheVolumeSegmentSize;
        mountDesc.metadataSize = s_PipelineCacheVolumeMetadataSize;
    }

    return outVolume.mount(mountDesc);
}

template<typename CacheDataVector>
static bool ValidatePipelineCacheData(const CacheDataVector& cacheData, const VkPhysicalDeviceProperties& properties){
    static_assert(IsSame_V<typename CacheDataVector::value_type, u8>, "pipeline cache data must be byte-addressable");

    if(cacheData.size() < sizeof(VkPipelineCacheHeaderVersionOne))
        return false;

    VkPipelineCacheHeaderVersionOne header{};
    NWB_MEMCPY(&header, sizeof(header), cacheData.data(), sizeof(header));

    if(header.headerSize < sizeof(VkPipelineCacheHeaderVersionOne))
        return false;
    if(header.headerSize > cacheData.size())
        return false;
    if(header.headerVersion != VK_PIPELINE_CACHE_HEADER_VERSION_ONE)
        return false;
    if(header.vendorID != properties.vendorID || header.deviceID != properties.deviceID)
        return false;
    if(NWB_MEMCMP(header.pipelineCacheUUID, properties.pipelineCacheUUID, VK_UUID_SIZE) != 0)
        return false;

    return true;
}

template<typename CacheDataVector>
static bool RetrievePipelineCacheData(VkDevice device, VkPipelineCache pipelineCache, CacheDataVector& outData){
    static_assert(IsSame_V<typename CacheDataVector::value_type, u8>, "pipeline cache data must be byte-addressable");

    outData.clear();

    for(usize attempt = 0; attempt < s_PipelineCacheDataMaxAttempts; ++attempt){
        size_t cacheSize = 0;
        VkResult res = vkGetPipelineCacheData(device, pipelineCache, &cacheSize, nullptr);
        if(res != VK_SUCCESS){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to query pipeline cache data size. {}"), ResultToString(res));
            return false;
        }
        if(cacheSize == 0)
            return true;
        if(cacheSize > static_cast<size_t>(Limit<usize>::s_Max)){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Pipeline cache data size {} exceeds runtime buffer limit {}.")
                , static_cast<u64>(cacheSize)
                , static_cast<u64>(Limit<usize>::s_Max)
            );
            return false;
        }

        outData.resize(static_cast<usize>(cacheSize));
        size_t retrievedSize = cacheSize;
        res = vkGetPipelineCacheData(device, pipelineCache, &retrievedSize, outData.data());
        if(res == VK_SUCCESS){
            if(retrievedSize > cacheSize || retrievedSize > static_cast<size_t>(Limit<usize>::s_Max)){
                outData.clear();
                NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Driver returned an invalid pipeline cache data size while serializing."));
                return false;
            }

            outData.resize(static_cast<usize>(retrievedSize));
            return true;
        }
        if(res == VK_INCOMPLETE)
            continue;

        outData.clear();
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to retrieve pipeline cache data. {}"), ResultToString(res));
        return false;
    }

    outData.clear();
    NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Pipeline cache data kept changing while serializing."));
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Device::Device(const DeviceDesc& desc)
    : RefCounter<GraphicsResource>(desc.threadPool)
    , m_gpuCrashDiagnosticsEnabled(desc.gpuCrashDiagnosticsEnabled)
    , m_gpuCrashTracker(desc.allocator.getObjectArena())
    , m_gpuCrashReportArena(VulkanArenaScope::s_GpuCrashReportArena, Alloc::PersistentArena::StructureAlignedSize(s_GpuCrashReportArenaSize))
    , m_gpuCrashVendorBinaryArena(VulkanArenaScope::s_GpuCrashVendorBinaryArena, Alloc::PersistentArena::StructureAlignedSize(s_MaxDeviceFaultVendorBinaryBytes))
    , m_context(desc.allocator, desc.threadPool, desc.instance, desc.physicalDevice, desc.device, desc.allocationCallbacks)
    , m_allocator(m_context)
    , m_descriptorBufferManager(m_context, m_allocator)
    , m_gpuDescriptorHeap(*this)
    , m_pipelineCacheDirectory(m_context.objectArena, desc.pipelineCacheDirectory)
    , m_pipelineCacheVolumeName(m_context.objectArena)
    , m_uploadManager(*this, s_DefaultUploadChunkSize, 0, false)
    , m_scratchManager(*this, s_DefaultScratchChunkSize, s_ScratchMemoryLimit, true)
{
    VkResult res = VK_SUCCESS;

    m_context.descriptorBufferManager = &m_descriptorBufferManager;
    m_context.graphicsQueueFamilyIndex = desc.graphicsQueueIndex;
    m_context.asyncComputeQueueFamilyIndex = desc.asyncComputeLaneEnabled ? desc.computeQueueIndex : s_InvalidQueueFamilyIndex;
    m_context.transferQueueFamilyIndex = desc.transferQueueEnabled ? desc.transferQueueIndex : s_InvalidQueueFamilyIndex;
    m_context.asyncComputeLaneEnabled = desc.asyncComputeLaneEnabled;
    m_context.transferQueueEnabled = desc.transferQueueEnabled;

    vkGetPhysicalDeviceProperties(m_context.physicalDevice, &m_context.physicalDeviceProperties);
    vkGetPhysicalDeviceMemoryProperties(m_context.physicalDevice, &m_context.memoryProperties);
    m_pipelineCacheVolumeName.assign(VulkanDetail::s_PipelineCacheVolumeName);

    m_context.extensions.buffer_device_address = desc.bufferDeviceAddressSupported;
    m_context.extensions.KHR_dynamic_rendering = desc.dynamicRenderingSupported;
    m_context.extensions.KHR_synchronization2 = desc.synchronization2Supported;
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

    m_context.meshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
    if(m_context.extensions.EXT_mesh_shader){
        m_context.meshShaderFeatures.meshShader = VK_TRUE;
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
            || !vkCmdCopyAccelerationStructureKHR
            || !vkCmdWriteAccelerationStructuresPropertiesKHR
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
        auto breadcrumbInfo = VulkanDetail::MakeVkStruct<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
        breadcrumbInfo.size = static_cast<VkDeviceSize>(s_MaxAmdBreadcrumbSlots) * sizeof(u32);
        breadcrumbInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        breadcrumbInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        const VkResult breadcrumbRes = m_allocator.createHostMappedBuffer(m_amdBreadcrumb.buffer, m_amdBreadcrumb.allocation, m_amdBreadcrumb.mappedMemory, breadcrumbInfo);
        if(breadcrumbRes == VK_SUCCESS && m_amdBreadcrumb.mappedMemory){
            NWB_MEMSET(m_amdBreadcrumb.mappedMemory, 0, static_cast<usize>(breadcrumbInfo.size));
        }
        else{
            // Release an unusable allocation before disabling breadcrumbs.
            if(breadcrumbRes == VK_SUCCESS)
                m_allocator.destroyHostMappedBuffer(m_amdBreadcrumb.buffer, m_amdBreadcrumb.allocation, m_amdBreadcrumb.mappedMemory);
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to allocate AMD breadcrumb buffer ({}); AMD GPU breadcrumbs disabled."), ResultToString(breadcrumbRes));
            m_context.extensions.AMD_buffer_marker = false;
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

    if(desc.graphicsQueue && desc.graphicsQueueIndex >= 0){
        m_queues[static_cast<u32>(CommandQueue::Graphics)].emplace(m_context, *this, CommandQueue::Graphics, desc.graphicsQueue, desc.graphicsQueueIndex);
    }
    if(desc.computeQueue && desc.computeQueueIndex >= 0){
        m_queues[static_cast<u32>(CommandQueue::Compute)].emplace(m_context, *this, CommandQueue::Compute, desc.computeQueue, desc.computeQueueIndex);
    }
    if(desc.transferQueue && desc.transferQueueIndex >= 0){
        m_queues[static_cast<u32>(CommandQueue::Transfer)].emplace(m_context, *this, CommandQueue::Transfer, desc.transferQueue, desc.transferQueueIndex);
    }
}
Device::~Device(){
    waitForIdle();

    m_uploadManager.clear();
    m_scratchManager.clear();

    m_gpuDescriptorHeap.shutdown();
    m_descriptorBufferManager.shutdown();

    for(u32 i = 0; i < static_cast<u32>(CommandQueue::kCount); ++i)
        m_queues[i].reset();

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

bool Device::loadPipelineCacheData(GraphicsBytes& outData){
    outData.clear();
    if(m_pipelineCacheDirectory.empty() || m_pipelineCacheVolumeName.empty())
        return false;
    if(!::VolumeSegmentExists(m_pipelineCacheDirectory, m_pipelineCacheVolumeName))
        return false;

    Filesystem::VolumeFileSystem volume(m_context.objectArena);
    if(
        !VulkanDetail::MountPipelineCacheVolume(
            m_pipelineCacheDirectory,
            m_pipelineCacheVolumeName,
            false,
            Filesystem::VolumeUsage::RuntimeReadOnly,
            volume
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to mount pipeline cache runtime volume '{}' from '{}'.")
            , StringConvert(m_pipelineCacheVolumeName)
            , PathToString<tchar>(m_pipelineCacheDirectory)
        );
        return false;
    }

    const Name cachePath(VulkanDetail::s_PipelineCacheVirtualPath);
    if(!volume.fileExists(cachePath))
        return false;
    if(!volume.readFile(cachePath, outData)){
        outData.clear();
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to read pipeline cache data from runtime volume '{}'."), StringConvert(m_pipelineCacheVolumeName));
        return false;
    }
    if(!VulkanDetail::ValidatePipelineCacheData(outData, m_context.physicalDeviceProperties)){
        outData.clear();
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Ignoring incompatible pipeline cache data in runtime volume '{}'."), StringConvert(m_pipelineCacheVolumeName));
        return false;
    }

    NWB_LOGGER_INFO(NWB_TEXT("Vulkan: Loaded pipeline cache runtime volume '{}' ({} bytes).")
        , StringConvert(m_pipelineCacheVolumeName)
        , outData.size()
    );
    return true;
}

VkDescriptorSetLayout Device::getOrCreateEmptyDescriptorBufferSetLayout()const{
    // Lazily create immutable empty layouts under the cache mutex.
    if(m_context.emptyDescriptorBufferSetLayout != VK_NULL_HANDLE)
        return m_context.emptyDescriptorBufferSetLayout;

    if(!m_context.extensions.EXT_descriptor_buffer)
        return VK_NULL_HANDLE;

    auto layoutInfo = VulkanDetail::MakeVkStruct<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    layoutInfo.bindingCount = 0;
    layoutInfo.pBindings = nullptr;

    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    const VkResult res = vkCreateDescriptorSetLayout(m_context.device, &layoutInfo, m_context.allocationCallbacks, &setLayout);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create empty descriptor-buffer set layout. {}"), ResultToString(res));
        return VK_NULL_HANDLE;
    }
    const_cast<VkDescriptorSetLayout&>(m_context.emptyDescriptorBufferSetLayout) = setLayout;
    return setLayout;
}

void Device::savePipelineCacheData(){
    if(m_pipelineCacheDirectory.empty() || m_pipelineCacheVolumeName.empty() || !m_context.pipelineCache)
        return;

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_PipelineCacheSaveArena);
    Vector<u8, Alloc::ScratchArena> cacheData{scratchArena};
    if(!VulkanDetail::RetrievePipelineCacheData(m_context.device, m_context.pipelineCache, cacheData))
        return;
    if(cacheData.empty())
        return;

    if(!VulkanDetail::ValidatePipelineCacheData(cacheData, m_context.physicalDeviceProperties)){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Driver returned incompatible pipeline cache data; skipping runtime cache write."));
        return;
    }

    Filesystem::VolumeFileSystem volume(m_context.objectArena);
    if(
        !VulkanDetail::MountPipelineCacheVolume(
            m_pipelineCacheDirectory,
            m_pipelineCacheVolumeName,
            true,
            Filesystem::VolumeUsage::RuntimeReadWrite,
            volume
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to mount pipeline cache runtime volume '{}' for write at '{}'.")
            , StringConvert(m_pipelineCacheVolumeName)
            , PathToString<tchar>(m_pipelineCacheDirectory)
        );
        if(!Filesystem::RemoveVolumeSegments(m_pipelineCacheDirectory, m_pipelineCacheVolumeName)){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to remove unusable pipeline cache runtime volume '{}'."), StringConvert(m_pipelineCacheVolumeName));
            return;
        }
        if(
            !VulkanDetail::MountPipelineCacheVolume(
                m_pipelineCacheDirectory,
                m_pipelineCacheVolumeName,
                true,
                Filesystem::VolumeUsage::RuntimeReadWrite,
                volume
            )
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to recreate pipeline cache runtime volume '{}'."), StringConvert(m_pipelineCacheVolumeName));
            return;
        }
    }

    const Name cachePath(VulkanDetail::s_PipelineCacheVirtualPath);
    if(!volume.writeFile(cachePath, cacheData)){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to write pipeline cache data to runtime volume '{}'."), StringConvert(m_pipelineCacheVolumeName));
        return;
    }
    if(!volume.compact(true))
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to compact pipeline cache runtime volume '{}'."), StringConvert(m_pipelineCacheVolumeName));

    NWB_LOGGER_INFO(NWB_TEXT("Vulkan: Saved pipeline cache runtime volume '{}' ({} bytes).")
        , StringConvert(m_pipelineCacheVolumeName)
        , cacheData.size()
    );
}

Queue* Device::getQueue(CommandQueue::Enum queueType){
    auto index = static_cast<u32>(queueType);
    if(index < static_cast<u32>(CommandQueue::kCount))
        return m_queues[index] ? &*m_queues[index] : nullptr;
    return nullptr;
}

#if !defined(NWB_FINAL) || defined(NWB_ENABLE_TEST_FEATURE_OVERRIDES)

void Device::rejectNextSubmissionForTesting(const CommandQueue::Enum queue){
    const u32 index = static_cast<u32>(queue);
    if(index >= static_cast<u32>(CommandQueue::kCount))
        return;

    m_submissionRejectionsForTesting[index].fetch_add(1u, MemoryOrder::relaxed);
}

void Device::clearSubmissionRejectionsForTesting(){
    for(Atomic<u32>& count : m_submissionRejectionsForTesting)
        count.store(0u, MemoryOrder::relaxed);
}

bool Device::consumeSubmissionRejectionForTesting(const CommandQueue::Enum queue){
    const u32 index = static_cast<u32>(queue);
    if(index >= static_cast<u32>(CommandQueue::kCount))
        return false;

    Atomic<u32>& count = m_submissionRejectionsForTesting[index];
    u32 pending = count.load(MemoryOrder::relaxed);
    while(pending > 0u){
        if(count.compare_exchange_weak(pending, pending - 1u, MemoryOrder::relaxed))
            return true;
    }
    return false;
}

#endif


CommandListHandle Device::createCommandList(const CommandListParameters& params){
    CommandListParameters resolvedParams = params;
    if(resolvedParams.resolveRenderLane){
        resolvedParams.queueType = resolveRenderLane(resolvedParams.renderLane);
        resolvedParams.resolveRenderLane = false;
    }

    auto* cmdList = NewArenaObject<CommandList>(m_context.objectArena, *this, resolvedParams);
    return CommandListHandle(cmdList, CommandListHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

u64 Device::executeCommandLists(
    CommandList* const* pCommandLists,
    const usize numCommandLists,
    const CommandQueue::Enum executionQueue,
    bool* const outCommandListsSubmitted
){
    if(outCommandListsSubmitted)
        *outCommandListsSubmitted = false;

    // Device loss makes recovery submissions unsafe.
    if(isDeviceLost())
        return 0u;

    Queue* queue = getQueue(executionQueue);
    if(!queue){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to execute command lists: requested queue is not available"));
        return 0;
    }

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_CommandListExecuteArena);
    Vector<TrackedCommandBuffer*, Alloc::ScratchArena> submittedOwners{scratchArena};
    if(pCommandLists && numCommandLists > 0){
        submittedOwners.reserve(numCommandLists);
        for(usize i = 0; i < numCommandLists; ++i){
            if(!pCommandLists[i])
                continue;
            if(pCommandLists[i] && pCommandLists[i]->m_currentCmdBuf)
                submittedOwners.push_back(pCommandLists[i]->m_currentCmdBuf.get());
        }
    }

    bool submissionAccepted = false;
    const u64 submittedID = queue->submit(pCommandLists, numCommandLists, nullptr, 0u, &submissionAccepted);
    if(outCommandListsSubmitted)
        *outCommandListsSubmitted = submissionAccepted && !submittedOwners.empty();

    if(!submittedOwners.empty()){
        if(submissionAccepted){
            m_uploadManager.submitChunks(executionQueue, submittedID, submittedOwners.data(), submittedOwners.size());
            m_scratchManager.submitChunks(executionQueue, submittedID, submittedOwners.data(), submittedOwners.size());
        }
        else{
            const auto ownerStillRecorded = [&](TrackedCommandBuffer* owner) -> bool {
                if(!owner || !pCommandLists)
                    return false;
                for(usize i = 0; i < numCommandLists; ++i){
                    auto* cmdList = pCommandLists[i];
                    if(cmdList && cmdList->m_currentCmdBuf.get() == owner)
                        return true;
                }
                return false;
            };

            const u64 reusableVersion = queueGetCompletedInstance(executionQueue);
            for(TrackedCommandBuffer* owner : submittedOwners){
                if(ownerStillRecorded(owner))
                    continue;
                m_uploadManager.discardChunks(executionQueue, owner, reusableVersion);
                m_scratchManager.discardChunks(executionQueue, owner, reusableVersion);
            }
        }
    }

    return submittedID;
}

QueueSubmissionToken Device::executeCommandLists(
    CommandList* const* pCommandLists,
    const usize numCommandLists,
    const CommandQueue::Enum executionQueue,
    const QueueSubmissionDesc& submitDesc
){
    // Do not submit or wait after terminal device loss.
    if(isDeviceLost())
        return {};

    if(submitDesc.waitTokenCount > 0u && !submitDesc.waitTokens){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to execute command lists: submission wait token array is null"));
        return {};
    }

    Queue* const queue = getQueue(executionQueue);
    if(!queue){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to execute command lists: requested queue is not available"));
        return {};
    }

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_CommandListExecuteArena);
    Vector<Queue::SubmissionWait, Alloc::ScratchArena> localWaits{scratchArena};
    if(submitDesc.waitTokenCount > 0u){
        localWaits.reserve(submitDesc.waitTokenCount);
        for(usize i = 0u; i < submitDesc.waitTokenCount; ++i){
            const QueueSubmissionToken& token = submitDesc.waitTokens[i];
            if(!token.valid()){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to execute command lists: dependency token is not accepted"));
                return {};
            }

            Queue* const producerQueue = getQueue(token.queue);
            if(!producerQueue || producerQueue->m_trackingSemaphore == VK_NULL_HANDLE){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to execute command lists: dependency producer queue is unavailable"));
                return {};
            }

            // Reject fabricated/future timeline tokens that could deadlock a wait.
            {
                ScopedLock producerLock(producerQueue->m_mutex);
                if(token.value > producerQueue->m_lastSubmittedID){
                    NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to execute command lists: dependency token refers to an unsignalled timeline value"));
                    return {};
                }
            }

            // Queue order already covers same-queue dependencies.
            if(token.queue == executionQueue)
                continue;

            // Collapse same-semaphore waits to their largest timeline value.
            bool merged = false;
            for(Queue::SubmissionWait& wait : localWaits){
                if(wait.semaphore != producerQueue->m_trackingSemaphore)
                    continue;

                wait.value = Max(wait.value, token.value);
                merged = true;
                break;
            }
            if(!merged)
                localWaits.push_back(Queue::SubmissionWait{ producerQueue->m_trackingSemaphore, token.value });
        }
    }

    Vector<TrackedCommandBuffer*, Alloc::ScratchArena> submittedOwners{scratchArena};
    if(pCommandLists && numCommandLists > 0u){
        submittedOwners.reserve(numCommandLists);
        for(usize i = 0u; i < numCommandLists; ++i){
            if(pCommandLists[i] && pCommandLists[i]->m_currentCmdBuf)
                submittedOwners.push_back(pCommandLists[i]->m_currentCmdBuf.get());
        }
    }

    bool submissionAccepted = false;
    const u64 submittedID = queue->submit(
        pCommandLists,
        numCommandLists,
        localWaits.empty() ? nullptr : localWaits.data(),
        localWaits.size(),
        &submissionAccepted
    );

    if(!submittedOwners.empty()){
        if(submissionAccepted){
            m_uploadManager.submitChunks(executionQueue, submittedID, submittedOwners.data(), submittedOwners.size());
            m_scratchManager.submitChunks(executionQueue, submittedID, submittedOwners.data(), submittedOwners.size());
        }
        else{
            const auto ownerStillRecorded = [&](TrackedCommandBuffer* owner) -> bool {
                if(!owner || !pCommandLists)
                    return false;
                for(usize i = 0u; i < numCommandLists; ++i){
                    CommandList* const cmdList = pCommandLists[i];
                    if(cmdList && cmdList->m_currentCmdBuf.get() == owner)
                        return true;
                }
                return false;
            };

            const u64 reusableVersion = queueGetCompletedInstance(executionQueue);
            for(TrackedCommandBuffer* owner : submittedOwners){
                if(ownerStillRecorded(owner))
                    continue;
                m_uploadManager.discardChunks(executionQueue, owner, reusableVersion);
                m_scratchManager.discardChunks(executionQueue, owner, reusableVersion);
            }
        }
    }

    if(!submissionAccepted)
        return {};

    return QueueSubmissionToken{ executionQueue, submittedID };
}

QueueSubmissionToken Device::executeCommandLists(
    CommandList* const* pCommandLists,
    const usize numCommandLists,
    const RenderLane::Enum executionLane,
    const QueueSubmissionDesc& submitDesc
){
    return executeCommandLists(pCommandLists, numCommandLists, resolveRenderLane(executionLane), submitDesc);
}

CommandQueue::Enum Device::resolveRenderLane(const RenderLane::Enum lane)const{
    switch(lane){
    case RenderLane::Graphics:
        return CommandQueue::Graphics;
    case RenderLane::AsyncCompute:
        return isRenderLaneDedicated(lane) ? CommandQueue::Compute : CommandQueue::Graphics;
    default:
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: invalid render lane"));
        return CommandQueue::Graphics;
    }
}

bool Device::isRenderLaneDedicated(const RenderLane::Enum lane)const{
    return
        lane == RenderLane::AsyncCompute
        && m_context.asyncComputeLaneEnabled
        && m_queues[static_cast<u32>(CommandQueue::Compute)].has_value()
    ;
}

u32 Device::getQueueFamilyIndex(const CommandQueue::Enum queueType)const{
    const u32 index = static_cast<u32>(queueType);
    if(index >= static_cast<u32>(CommandQueue::kCount) || !m_queues[index])
        return VK_QUEUE_FAMILY_IGNORED;

    return m_queues[index]->m_queueFamilyIndex;
}

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

    for(u32 i = 0; i < static_cast<u32>(CommandQueue::kCount); ++i){
        if(m_queues[i])
            m_queues[i]->waitForIdle();
    }

    return true;
}

void Device::captureGpuCrash(const AStringView context)noexcept{
    // Device-loss state must not depend on optional crash diagnostics.
    m_deviceLost.store(true, MemoryOrder::release);
    if(!m_gpuCrashDiagnosticsEnabled)
        return;

    const bool hasCheckpoints = m_context.extensions.NV_device_diagnostic_checkpoints;
    const bool hasDeviceFault = m_context.extensions.EXT_device_fault;
    const bool hasBufferMarker = m_context.extensions.AMD_buffer_marker && m_amdBreadcrumb.buffer != VK_NULL_HANDLE;

    // Capture only the first concurrent device-loss report.
    if(m_gpuCrashCaptured.exchange(true))
        return;

    GpuCrashReport report(m_gpuCrashReportArena);
    Vector<u8, Alloc::PersistentArena> vendorBinary(m_gpuCrashVendorBinaryArena);

    // Fixed crash arena permits partial reports without allocation failure.
    try{
        report.details.reserve(s_MaxGpuCrashReportChars);
        report.context.append(context.data(), context.size());

        // One aggregate budget bounds all fault sections.
        u32 remainingEntries = s_MaxGpuCrashCaptureEntries;

        if(hasCheckpoints){
            for(u32 queueIndex = 0; queueIndex < static_cast<u32>(CommandQueue::kCount) && remainingEntries > 0u; ++queueIndex){
                if(!m_queues[queueIndex])
                    continue;

                VkQueue queue = m_queues[queueIndex]->m_queue;
                uint32_t checkpointCount = 0;
                vkGetQueueCheckpointDataNV(queue, &checkpointCount, nullptr);
                if(checkpointCount == 0)
                    continue;
                if(checkpointCount > remainingEntries)
                    checkpointCount = remainingEntries;

                Vector<VkCheckpointDataNV, Alloc::PersistentArena> checkpoints(m_gpuCrashReportArena);
                checkpoints.resize(checkpointCount, VulkanDetail::MakeVkStruct<VkCheckpointDataNV>(VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV));
                vkGetQueueCheckpointDataNV(queue, &checkpointCount, checkpoints.data());

                for(const auto& checkpoint : checkpoints){
                    const usize markerHash = reinterpret_cast<usize>(checkpoint.pCheckpointMarker);
                    if(markerHash == 0)
                        continue;

                    const auto resolved = m_gpuCrashTracker.resolveMarker(markerHash);
                    if(!resolved.first())
                        continue;

                    report.details.append(StringFormat(m_gpuCrashReportArena, "last executed marker (stage 0x{:x}): {}\n", static_cast<u32>(checkpoint.stage), VulkanDetail::TrimGpuCrashText(resolved.second())));
                }

                remainingEntries -= checkpointCount;
            }
        }

        if(hasBufferMarker && remainingEntries > 0u){
            const u32* breadcrumbSlots = static_cast<const u32*>(m_amdBreadcrumb.mappedMemory);
            if(breadcrumbSlots){
                // Largest sequence marks the last GPU-reached breadcrumb.
                u32 furthestSequence = 0u;
                u32 furthestSlot = 0u;
                for(u32 slot = 0u; slot < s_MaxAmdBreadcrumbSlots; ++slot){
                    if(breadcrumbSlots[slot] > furthestSequence){
                        furthestSequence = breadcrumbSlots[slot];
                        furthestSlot = slot;
                    }
                }

                if(furthestSequence != 0u){
                    AmdBreadcrumbSlotRecord record;
                    {
                        // Lock with breadcrumb writes to avoid torn records.
                        ScopedLock lock(m_amdBreadcrumb.slotMutex);
                        record = m_amdBreadcrumb.slotRecords[furthestSlot];
                    }
                    if(record.sequence == furthestSequence){
                        const auto resolved = m_gpuCrashTracker.resolveMarker(record.markerHash);
                        if(resolved.first())
                            report.details.append(StringFormat(m_gpuCrashReportArena, "last reached breadcrumb (seq {}): {}\n", furthestSequence, VulkanDetail::TrimGpuCrashText(resolved.second())));
                        else
                            report.details.append(StringFormat(m_gpuCrashReportArena, "last reached breadcrumb (seq {}): <unresolved marker>\n", furthestSequence));
                    }
                    else{
                        report.details.append(StringFormat(m_gpuCrashReportArena, "last reached breadcrumb (seq {}): <label overwritten>\n", furthestSequence));
                    }
                    --remainingEntries;
                }
            }
        }

        if(hasDeviceFault && remainingEntries > 0u){
            auto faultCounts = VulkanDetail::MakeVkStruct<VkDeviceFaultCountsEXT>(VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT);
            if(vkGetDeviceFaultInfoEXT(m_context.device, &faultCounts, nullptr) == VK_SUCCESS){
                const VkDeviceSize vendorBinaryByteSize = faultCounts.vendorBinarySize;
                const bool vendorBinaryIsRgd = m_context.physicalDeviceProperties.vendorID == s_AmdVendorId;
                if(faultCounts.addressInfoCount > remainingEntries)
                    faultCounts.addressInfoCount = remainingEntries;
                remainingEntries -= faultCounts.addressInfoCount;
                if(faultCounts.vendorInfoCount > remainingEntries)
                    faultCounts.vendorInfoCount = remainingEntries;
                remainingEntries -= faultCounts.vendorInfoCount;

                Vector<VkDeviceFaultAddressInfoEXT, Alloc::PersistentArena> addressInfos(m_gpuCrashReportArena);
                Vector<VkDeviceFaultVendorInfoEXT, Alloc::PersistentArena> vendorInfos(m_gpuCrashReportArena);
                addressInfos.resize(faultCounts.addressInfoCount, VkDeviceFaultAddressInfoEXT{});
                vendorInfos.resize(faultCounts.vendorInfoCount, VkDeviceFaultVendorInfoEXT{});
                if(vendorBinaryIsRgd && vendorBinaryByteSize != 0u && vendorBinaryByteSize <= static_cast<VkDeviceSize>(s_MaxDeviceFaultVendorBinaryBytes))
                    vendorBinary.resize(static_cast<usize>(vendorBinaryByteSize));

                auto faultInfo = VulkanDetail::MakeVkStruct<VkDeviceFaultInfoEXT>(VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT);
                faultInfo.pAddressInfos = addressInfos.empty() ? nullptr : addressInfos.data();
                faultInfo.pVendorInfos = vendorInfos.empty() ? nullptr : vendorInfos.data();
                faultInfo.pVendorBinaryData = vendorBinary.empty() ? nullptr : vendorBinary.data();

                const VkResult faultResult = vkGetDeviceFaultInfoEXT(m_context.device, &faultCounts, &faultInfo);
                if(faultResult == VK_SUCCESS || faultResult == VK_INCOMPLETE){
                    const char* faultDescription = faultInfo.description;
                    report.details.append(StringFormat(m_gpuCrashReportArena, "device fault: {}\n", VulkanDetail::TrimGpuCrashText(faultDescription)));
                    if(vendorBinaryByteSize != 0u){
                        if(!vendorBinary.empty()){
                            report.details.append(StringFormat(m_gpuCrashReportArena, "device fault vendor binary (RGD): {} bytes\n", vendorBinary.size()));
                            report.binaryDumpKind = GpuCrashDumpKind::RadeonGpuDetective;
                            report.binaryDump = vendorBinary.data();
                            report.binaryDumpSize = vendorBinary.size();
                        }
                        else if(!vendorBinaryIsRgd){
                            report.details.append(StringFormat(m_gpuCrashReportArena, "device fault vendor binary not packaged: {} bytes from vendor 0x{:x}\n", static_cast<u64>(vendorBinaryByteSize), static_cast<u32>(m_context.physicalDeviceProperties.vendorID)));
                        }
                        else{
                            report.details.append(StringFormat(m_gpuCrashReportArena, "device fault vendor binary skipped: {} bytes exceeds {} byte cap\n", static_cast<u64>(vendorBinaryByteSize), static_cast<u64>(s_MaxDeviceFaultVendorBinaryBytes)));
                        }
                    }

                    for(u32 i = 0; i < faultCounts.addressInfoCount; ++i){
                        const VkDeviceFaultAddressInfoEXT& addressInfo = addressInfos[i];
                        report.details.append(StringFormat(m_gpuCrashReportArena, "fault address 0x{:x} (type {}, precision 0x{:x})\n"
                            , static_cast<u64>(addressInfo.reportedAddress)
                            , static_cast<u32>(addressInfo.addressType)
                            , static_cast<u64>(addressInfo.addressPrecision)
                        ));
                    }

                    for(u32 i = 0; i < faultCounts.vendorInfoCount; ++i){
                        const VkDeviceFaultVendorInfoEXT& vendorInfo = vendorInfos[i];
                        const char* vendorDescription = vendorInfo.description;
                        report.details.append(StringFormat(m_gpuCrashReportArena, "vendor fault '{}' (code 0x{:x}, data 0x{:x})\n"
                            , VulkanDetail::TrimGpuCrashText(vendorDescription)
                            , static_cast<u64>(vendorInfo.vendorFaultCode)
                            , static_cast<u64>(vendorInfo.vendorFaultData)
                        ));
                    }
                }
            }
        }

        if(report.details.empty())
            report.details.append(StringFormat(m_gpuCrashReportArena,
                "minimal GPU crash report: no vendor GPU dump or device-fault details were available\n"
                "capture context: {}\n"
                "device: {} (vendor 0x{:x}, device 0x{:x}, driver 0x{:x})\n"
                "diagnostic paths: NV_device_diagnostic_checkpoints={}, AMD_buffer_marker={}, VK_EXT_device_fault={}, NVIDIA Aftermath={}\n"
                , VulkanDetail::TrimGpuCrashText(context)
                , VulkanDetail::TrimGpuCrashText(m_context.physicalDeviceProperties.deviceName)
                , static_cast<u32>(m_context.physicalDeviceProperties.vendorID)
                , static_cast<u32>(m_context.physicalDeviceProperties.deviceID)
                , static_cast<u32>(m_context.physicalDeviceProperties.driverVersion)
                , VulkanDetail::GpuCrashAvailabilityText(hasCheckpoints)
                , VulkanDetail::GpuCrashAvailabilityText(hasBufferMarker)
                , VulkanDetail::GpuCrashAvailabilityText(hasDeviceFault)
                , VulkanDetail::GpuCrashAvailabilityText(Aftermath::IsActive())
            ));
    }
    catch(...){
    }

    try{
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: GPU crash detected during {}:\n{}"), StringConvert(report.context.c_str()), StringConvert(report.details.c_str()));
    }
    catch(...){
    }

    // Attach available Aftermath dump while its bytes remain owned by the module.
    if(Aftermath::IsActive()){
        const Aftermath::GpuCrashDumpView dump = Aftermath::WaitForCrashDump();
        if(dump.data && dump.size != 0u){
            report.binaryDumpKind = GpuCrashDumpKind::Aftermath;
            report.binaryDump = dump.data;
            report.binaryDumpSize = dump.size;
        }
    }

    try{
        DispatchGpuCrash(report);
    }
    catch(...){
    }
}

Device::AmdBreadcrumbWrite Device::reserveAmdBreadcrumb(const usize markerHash){
    AmdBreadcrumbWrite write;
    if(m_amdBreadcrumb.buffer == VK_NULL_HANDLE)
        return write;

    // Monotonic sequence maps GPU progress to breadcrumb markers.
    const u32 sequence = m_amdBreadcrumb.nextSequence.fetch_add(1u) + 1u;
    const u32 slot = sequence % s_MaxAmdBreadcrumbSlots;
    {
        // Serialize paired breadcrumb stores to avoid torn readback.
        ScopedLock lock(m_amdBreadcrumb.slotMutex);
        m_amdBreadcrumb.slotRecords[slot].markerHash = markerHash;
        m_amdBreadcrumb.slotRecords[slot].sequence = sequence;
    }

    write.buffer = m_amdBreadcrumb.buffer;
    write.offset = static_cast<VkDeviceSize>(slot) * sizeof(u32);
    write.marker = sequence;
    write.valid = true;
    return write;
}

void Device::runGarbageCollection(){
    // Avoid extra queue queries after device loss.
    if(isDeviceLost())
        return;

    for(u32 i = 0; i < static_cast<u32>(CommandQueue::kCount); ++i){
        if(m_queues[i]){
            ScopedLock lock(m_queues[i]->m_mutex);
            m_queues[i]->updateLastFinishedID();
        }
    }
}


bool Device::queryFeatureSupport(Feature::Enum feature, void* featureInfo, usize featureInfoSize){
    switch(feature){
    case Feature::DeferredCommandLists:
        return true;
    case Feature::RayTracingAccelStruct:
        return m_context.extensions.KHR_acceleration_structure && m_context.accelerationStructureFeatureEnabled;
    case Feature::RayTracingPipeline:
        return
            m_context.extensions.KHR_ray_tracing_pipeline
            && m_context.rayTracingPipelineFeatureEnabled
            && m_context.extensions.KHR_acceleration_structure
            && m_context.accelerationStructureFeatureEnabled
        ;
    case Feature::RayQuery:
        return
            m_context.extensions.KHR_ray_query
            && m_context.rayQueryFeatureEnabled
            && m_context.extensions.KHR_acceleration_structure
            && m_context.accelerationStructureFeatureEnabled
        ;
    case Feature::ShaderExecutionReordering:
        return
            (m_context.extensions.EXT_ray_tracing_invocation_reorder && m_context.rayTracingInvocationReorderExtFeatureEnabled)
            || (m_context.extensions.NV_ray_tracing_invocation_reorder && m_context.rayTracingInvocationReorderFeatureEnabled)
        ;
    case Feature::Spheres:
        return
            m_context.extensions.NV_ray_tracing_linear_swept_spheres
            && m_context.rayTracingLinearSweptSpheresFeatures.spheres == VK_TRUE
            && queryFeatureSupport(Feature::RayTracingPipeline)
        ;
    case Feature::LinearSweptSpheres:
        return
            m_context.extensions.NV_ray_tracing_linear_swept_spheres
            && m_context.rayTracingLinearSweptSpheresFeatures.linearSweptSpheres == VK_TRUE
            && queryFeatureSupport(Feature::RayTracingPipeline)
        ;
    case Feature::RayTracingOpacityMicromap:
        return
            m_context.extensions.EXT_opacity_micromap
            && m_context.opacityMicromapFeatureEnabled
            && m_context.extensions.KHR_synchronization2
            && m_context.extensions.KHR_acceleration_structure
            && m_context.accelerationStructureFeatureEnabled
        ;
    case Feature::RayTracingClusters:
        return
            m_context.extensions.NV_cluster_acceleration_structure
            && m_context.clusterAccelerationStructureFeatureEnabled
            && m_context.extensions.KHR_acceleration_structure
            && m_context.accelerationStructureFeatureEnabled
        ;
    case Feature::SamplerFeedback:
    case Feature::VirtualResources:
        // Retained unsupported feature ordinal for ABI compatibility.
        return false;
    case Feature::CooperativeVectorInferencing:
        return
            m_context.extensions.NV_cooperative_vector
            && m_context.coopVecFeatures.cooperativeVector == VK_TRUE
            && vkGetPhysicalDeviceCooperativeVectorPropertiesNV
            && vkConvertCooperativeVectorMatrixNV
            && vkCmdConvertCooperativeVectorMatrixNV
        ;
    case Feature::CooperativeVectorTraining:
        return
            m_context.extensions.NV_cooperative_vector
            && m_context.coopVecFeatures.cooperativeVector == VK_TRUE
            && m_context.coopVecFeatures.cooperativeVectorTraining == VK_TRUE
            && vkGetPhysicalDeviceCooperativeVectorPropertiesNV
            && vkConvertCooperativeVectorMatrixNV
            && vkCmdConvertCooperativeVectorMatrixNV
        ;
    case Feature::Meshlets:
        return m_context.extensions.EXT_mesh_shader && m_context.meshShaderFeatures.meshShader == VK_TRUE && vkCmdDrawMeshTasksEXT;
    case Feature::VariableRateShading:
        return m_context.extensions.KHR_fragment_shading_rate;
    case Feature::WaveLaneCountMinMax:{
        auto* out = static_cast<WaveLaneCountMinMaxFeatureInfo*>(featureInfo);
        if(out && featureInfoSize >= sizeof(WaveLaneCountMinMaxFeatureInfo)){
            out->minWaveLaneCount = m_context.subgroupProperties.subgroupSize;
            out->maxWaveLaneCount = m_context.subgroupProperties.subgroupSize;
        }
        return true;
    }
    case Feature::ConstantBufferRanges:
        return true;
    default:
        return false;
    }
}

bool Device::canCreateSampledTextureFormat(const Format::Enum format)const{
    const VkFormat vkFormat = ConvertFormat(format);
    if(vkFormat == VK_FORMAT_UNDEFINED)
        return false;

    auto imageFormatInfo = VulkanDetail::MakeVkStruct<VkPhysicalDeviceImageFormatInfo2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2);
    imageFormatInfo.format = vkFormat;
    imageFormatInfo.type = VK_IMAGE_TYPE_2D;
    imageFormatInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    // Static textures must be uploadable as well as sampled, so verify both uses together.
    imageFormatInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    auto imageFormatProperties = VulkanDetail::MakeVkStruct<VkImageFormatProperties2>(VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2);
    const VkResult res = vkGetPhysicalDeviceImageFormatProperties2(
        m_context.physicalDevice,
        &imageFormatInfo,
        &imageFormatProperties
    );
    if(res == VK_SUCCESS)
        return true;

    if(res != VK_ERROR_FORMAT_NOT_SUPPORTED){
        NWB_LOGGER_WARNING(
            NWB_TEXT("Vulkan: Failed to probe sampled texture format {}: {}"),
            StringConvert(GetFormatInfo(format).name),
            ResultToString(res)
        );
    }
    return false;
}

FormatSupport::Mask Device::queryFormatSupportUncached(const Format::Enum format)const{
    if(Format::IsASTCHdrFormat(format) && !m_context.extensions.EXT_texture_compression_astc_hdr)
        return FormatSupport::None;

    const VkFormat vkFormat = ConvertFormat(format);
    if(vkFormat == VK_FORMAT_UNDEFINED)
        return FormatSupport::None;

    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(m_context.physicalDevice, vkFormat, &props);

    FormatSupport::Mask support = FormatSupport::None;

    VkFormatFeatureFlags features = props.optimalTilingFeatures;

    if(features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)
        support |= FormatSupport::Texture;
    if(features & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        support |= FormatSupport::DepthStencil;
    if(features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
        support |= FormatSupport::RenderTarget;
    if(features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT)
        support |= FormatSupport::Blendable;
    if(features & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)
        support |= FormatSupport::ShaderUavStore;
    if(features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
        support |= FormatSupport::ShaderSample;

    VkFormatFeatureFlags bufferFeatures = props.bufferFeatures;
    if(bufferFeatures & VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT)
        support |= FormatSupport::Buffer;
    if(bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)
        support |= FormatSupport::Buffer;

    return support;
}

void Device::probeCompressedTextureFormats(){
    constexpr FormatSupport::Mask requiredReadableSupport = FormatSupport::Texture | FormatSupport::ShaderSample;

    u32 readableAstcLdrFormatCount = 0u;
    u32 readableAstcHdrFormatCount = 0u;
    u32 readableBcFormatCount = 0u;
    u32 astcLdrFormatCount = 0u;
    u32 astcHdrFormatCount = 0u;
    u32 bcFormatCount = 0u;
    for(u32 formatValue = static_cast<u32>(Format::BC1_UNORM); formatValue <= static_cast<u32>(Format::ASTC_12x12_FLOAT); ++formatValue){
        const Format::Enum format = static_cast<Format::Enum>(formatValue);
        FormatSupport::Mask support = queryFormatSupportUncached(format);
        if(
            (support & FormatSupport::Texture) == FormatSupport::Texture
            && !canCreateSampledTextureFormat(format)
        ){
            support &= ~requiredReadableSupport;
        }
        m_compressedFormatSupport[formatValue] = support;

        const bool readable = (support & requiredReadableSupport) == requiredReadableSupport;
        if(Format::IsASTCCompressedFormat(format)){
            if(Format::IsASTCHdrFormat(format)){
                ++astcHdrFormatCount;
                if(readable)
                    ++readableAstcHdrFormatCount;
            }
            else{
                ++astcLdrFormatCount;
                if(readable)
                    ++readableAstcLdrFormatCount;
            }
        }
        else{
            NWB_ASSERT(Format::IsBCCompressedFormat(format));
            ++bcFormatCount;
            if(readable)
                ++readableBcFormatCount;
        }
    }

    NWB_LOGGER_INFO(
        NWB_TEXT("Vulkan: compressed texture probe found {}/{} readable ASTC LDR formats, {}/{} readable ASTC HDR formats, and {}/{} readable BC formats."),
        readableAstcLdrFormatCount,
        astcLdrFormatCount,
        readableAstcHdrFormatCount,
        astcHdrFormatCount,
        readableBcFormatCount,
        bcFormatCount
    );
}

FormatSupport::Mask Device::queryFormatSupport(const Format::Enum format){
    if(Format::IsBlockCompressedFormat(format))
        return m_compressedFormatSupport[static_cast<usize>(format)];

    return queryFormatSupportUncached(format);
}

Object Device::getNativeQueue(ObjectType objectType, CommandQueue::Enum queue){
    if(objectType == ObjectTypes::VK_Queue){
        Queue* q = getQueue(queue);
        return q ? Object(q->m_queue) : Object(nullptr);
    }
    return Object(nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Heap::Heap(const VulkanContext& context, VulkanAllocator& allocator)
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_allocator(allocator)
{}
Heap::~Heap(){
    m_allocator.freeHeap(*this);
}

Object Heap::getNativeHandle(ObjectType objectType){
    if(objectType == ObjectTypes::VK_DeviceMemory)
        return Object(m_memory);
    return Object(nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


HeapHandle Device::createHeap(const HeapDesc& d){
    VkResult res = VK_SUCCESS;

    if(d.capacity == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create heap: capacity is zero"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create heap: capacity is zero"));
        return nullptr;
    }

    switch(d.type){
    case HeapType::DeviceLocal:
    case HeapType::Upload:
    case HeapType::Readback:
        break;
    default:
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create heap: invalid heap type"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create heap: invalid heap type"));
        return nullptr;
    }

    auto* heap = NewArenaObject<Heap>(m_context.objectArena, m_context, m_allocator);
    heap->m_desc = d;

    res = m_allocator.allocateHeap(*heap);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate heap memory ({} bytes): {}"), d.capacity, ResultToString(res));
        DestroyArenaObject(m_context.objectArena, heap);
        return nullptr;
    }

    return HeapHandle(heap, HeapHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CooperativeVectorDeviceFeatures Device::queryCoopVecFeatures(){
    VkResult res = VK_SUCCESS;

    CooperativeVectorDeviceFeatures output(m_context.objectArena);

    if(!m_context.extensions.NV_cooperative_vector || !m_context.coopVecFeatures.cooperativeVector)
        return output;

    uint32_t propertyCount = 0;
    res = vkGetPhysicalDeviceCooperativeVectorPropertiesNV(m_context.physicalDevice, &propertyCount, nullptr);
    if(res != VK_SUCCESS || propertyCount == 0)
        return output;

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_CooperativeVectorQueryArena);
    Vector<VkCooperativeVectorPropertiesNV, Alloc::ScratchArena> properties(propertyCount, scratchArena);
    for(u32 i = 0; i < propertyCount; ++i){
        properties[i].sType = VK_STRUCTURE_TYPE_COOPERATIVE_VECTOR_PROPERTIES_NV;
        properties[i].pNext = nullptr;
    }

    res = vkGetPhysicalDeviceCooperativeVectorPropertiesNV(m_context.physicalDevice, &propertyCount, properties.data());
    if(res != VK_SUCCESS)
        return output;

    output.matMulFormats.resize(propertyCount);
    auto fillMatMulFormat = [&](usize i){
        const auto& prop = properties[i];
        CooperativeVectorMatMulFormatCombo& combo = output.matMulFormats[i];
        combo.inputType = VulkanDetail::ConvertCoopVecDataType(static_cast<VkComponentTypeKHR>(prop.inputType));
        combo.inputInterpretation = VulkanDetail::ConvertCoopVecDataType(static_cast<VkComponentTypeKHR>(prop.inputInterpretation));
        combo.matrixInterpretation = VulkanDetail::ConvertCoopVecDataType(static_cast<VkComponentTypeKHR>(prop.matrixInterpretation));
        combo.biasInterpretation = VulkanDetail::ConvertCoopVecDataType(static_cast<VkComponentTypeKHR>(prop.biasInterpretation));
        combo.outputType = VulkanDetail::ConvertCoopVecDataType(static_cast<VkComponentTypeKHR>(prop.resultType));
        combo.transposeSupported = prop.transpose != VK_FALSE;
    };

    if(taskPool().isParallelEnabled() && propertyCount >= s_ParallelCoopVecThreshold)
        scheduleParallelFor(static_cast<usize>(0), propertyCount, fillMatMulFormat);
    else{
        for(usize i = 0; i < propertyCount; ++i)
            fillMatMulFormat(i);
    }

    output.trainingFloat16 =
        m_context.coopVecFeatures.cooperativeVectorTraining != VK_FALSE
        && m_context.coopVecProperties.cooperativeVectorTrainingFloat16Accumulation != VK_FALSE
    ;
    output.trainingFloat32 =
        m_context.coopVecFeatures.cooperativeVectorTraining != VK_FALSE
        && m_context.coopVecProperties.cooperativeVectorTrainingFloat32Accumulation != VK_FALSE
    ;

    return output;
}

usize Device::getCoopVecMatrixSize(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, i32 rows, i32 columns){
    VkResult res = VK_SUCCESS;

    if(!m_context.extensions.NV_cooperative_vector || !m_context.coopVecFeatures.cooperativeVector)
        return 0;
    if(rows <= 0 || columns <= 0)
        return 0;

    usize dstSize = 0;
    usize dataTypeSize = GetCooperativeVectorDataTypeSize(type);
    const usize rowCount = static_cast<usize>(rows);
    const usize columnCount = static_cast<usize>(columns);
    if(rowCount > (Limit<usize>::s_Max / columnCount))
        return 0;

    const usize elementCount = rowCount * columnCount;
    if(dataTypeSize > (Limit<usize>::s_Max / elementCount))
        return 0;

    auto convertInfo = VulkanDetail::MakeVkStruct<VkConvertCooperativeVectorMatrixInfoNV>(VK_STRUCTURE_TYPE_CONVERT_COOPERATIVE_VECTOR_MATRIX_INFO_NV);
    convertInfo.srcSize = dataTypeSize * elementCount;
    convertInfo.srcData.hostAddress = nullptr;
    convertInfo.pDstSize = &dstSize;
    convertInfo.dstData.hostAddress = nullptr;
    convertInfo.srcComponentType = VulkanDetail::ConvertCoopVecDataType(type);
    convertInfo.dstComponentType = convertInfo.srcComponentType;
    convertInfo.numRows = static_cast<u32>(rows);
    convertInfo.numColumns = static_cast<u32>(columns);
    convertInfo.srcLayout = VK_COOPERATIVE_VECTOR_MATRIX_LAYOUT_ROW_MAJOR_NV;
    convertInfo.srcStride = dataTypeSize * columns;
    convertInfo.dstLayout = VulkanDetail::ConvertCoopVecMatrixLayout(layout);
    convertInfo.dstStride = GetCooperativeVectorOptimalMatrixStride(type, layout, rows, columns);

    res = vkConvertCooperativeVectorMatrixNV(m_context.device, &convertInfo);
    if(res == VK_SUCCESS)
        return dstSize;

    return 0;
}

DeviceHandle CreateDevice(const DeviceDesc& desc){
    auto* device = NewArenaObject<Device>(desc.allocator.getObjectArena(), desc);
    return DeviceHandle(device, DeviceHandle::deleter_type(&desc.allocator.getObjectArena()), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

