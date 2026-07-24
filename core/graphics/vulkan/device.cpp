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
// A single, fixed runtime pipeline-cache volume (NOT keyed by device identity). The header UUID/vendor/device
// check in ValidatePipelineCacheData already rejects data from a different GPU/driver, so a device or driver
// change simply starts empty + overwrites this same volume on save.
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


static bool RuntimeCacheVolumeExists(const Path& directory, const AStringView volumeName){
    if(directory.empty())
        return false;

    ErrorCode errorCode;
    if(!IsDirectory(directory, errorCode) || errorCode)
        return false;

    const Path segmentPath = directory / ::MakeVolumeSegmentFileName(volumeName, 0).c_str();
    errorCode.clear();
    return FileExists(segmentPath, errorCode) && !errorCode;
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
    , m_descriptorHeapManager(m_context, m_allocator)
    , m_gpuDescriptorHeap(*this)
    , m_pipelineCacheDirectory(m_context.objectArena, desc.pipelineCacheDirectory)
    , m_pipelineCacheVolumeName(m_context.objectArena)
    , m_uploadManager(*this, s_DefaultUploadChunkSize, 0, false)
    , m_scratchManager(*this, s_DefaultScratchChunkSize, s_ScratchMemoryLimit, true)
{
    VkResult res = VK_SUCCESS;

    m_context.descriptorHeapManager = &m_descriptorHeapManager;

    vkGetPhysicalDeviceProperties(m_context.physicalDevice, &m_context.physicalDeviceProperties);
    vkGetPhysicalDeviceMemoryProperties(m_context.physicalDevice, &m_context.memoryProperties);
    m_pipelineCacheVolumeName.assign(VulkanDetail::s_PipelineCacheVolumeName);

    m_context.extensions.buffer_device_address = desc.bufferDeviceAddressSupported;
    m_context.extensions.KHR_dynamic_rendering = desc.dynamicRenderingSupported;
    m_context.extensions.KHR_synchronization2 = desc.synchronization2Supported;

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
        else if(NWB_STRCMP(ext, VK_EXT_DEBUG_MARKER_EXTENSION_NAME) == 0)
            m_context.extensions.EXT_debug_marker = true;
        else if(NWB_STRCMP(ext, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
            m_context.extensions.KHR_swapchain = true;
        else if(NWB_STRCMP(ext, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) == 0)
            m_context.extensions.KHR_dynamic_rendering = true;
        else if(NWB_STRCMP(ext, VK_EXT_DESCRIPTOR_HEAP_EXTENSION_NAME) == 0)
            m_context.extensions.EXT_descriptor_heap = true;
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

    if(m_context.extensions.EXT_debug_utils && (!vkCmdBeginDebugUtilsLabelEXT || !vkCmdEndDebugUtilsLabelEXT)){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Debug utils marker entry points are unavailable."));
        m_context.extensions.EXT_debug_utils = false;
    }

    if(m_context.extensions.EXT_debug_marker && (!vkCmdDebugMarkerBeginEXT || !vkCmdDebugMarkerEndEXT)){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Debug marker entry points are unavailable."));
        m_context.extensions.EXT_debug_marker = false;
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

        // Subgroup (wave) properties are core Vulkan 1.1 -- the engine floor is 1.3, so this is always available.
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

    if(m_context.extensions.NV_cooperative_vector){
        auto features2 = VulkanDetail::MakeVkStruct<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
        m_context.coopVecFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_VECTOR_FEATURES_NV;
        features2.pNext = &m_context.coopVecFeatures;
        vkGetPhysicalDeviceFeatures2(m_context.physicalDevice, &features2);
    }

    if(m_context.extensions.EXT_descriptor_heap){
        if(
            !vkGetPhysicalDeviceDescriptorSizeEXT
            || !vkWriteResourceDescriptorsEXT
            || !vkWriteSamplerDescriptorsEXT
            || !vkCmdBindResourceHeapEXT
            || !vkCmdBindSamplerHeapEXT
            || !vkCmdPushDataEXT
        ){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Descriptor heap entry points are unavailable, falling back to descriptor sets."));
            m_context.extensions.EXT_descriptor_heap = false;
        }
    }

    if(!m_allocator.initialize())
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to initialize VMA allocator"));

    // AMD breadcrumb ring: allocated AFTER the VMA allocator is initialized (createHostMappedBuffer needs it).
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
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to allocate AMD breadcrumb buffer ({}); AMD GPU breadcrumbs disabled."), ResultToString(breadcrumbRes));
            m_context.extensions.AMD_buffer_marker = false;
            m_amdBreadcrumb.buffer = VK_NULL_HANDLE;
        }
    }

    if(m_context.extensions.EXT_descriptor_heap){
        auto props2 = VulkanDetail::MakeVkStruct<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
        m_context.descriptorHeapProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT;
        props2.pNext = &m_context.descriptorHeapProperties;
        vkGetPhysicalDeviceProperties2(m_context.physicalDevice, &props2);

        if(!m_descriptorHeapManager.initialize()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Descriptor heap initialization failed, falling back to descriptor sets."));
            m_context.extensions.EXT_descriptor_heap = false;
        }
    }

    // Bring the global descriptor heap live for every run. Descriptor indexing is the portable path, so initialize it
    // independently of optional descriptor-heap acceleration and NWB_DEBUG. Capacity 0 selects a default clamped to
    // the device's update-after-bind limits; initialization failures are logged for bindless consumers.
    {
        GpuDescriptorHeapDesc heapDesc;
        if(!m_gpuDescriptorHeap.initialize(heapDesc))
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Global GpuDescriptorHeap failed to initialize; bindless heap consumers will be unavailable."));
    }

    GraphicsBytes pipelineCacheInitialData{m_context.objectArena};
    // loadPipelineCacheData logs the genuine failures (corrupt / incompatible data, mount/read errors) itself
    // and returns false with empty data for the expected "no cache yet" case -- the first run, or any prior run
    // that exited without its graceful destructor save (e.g. killed / crashed). That cold start is normal, so it
    // is an INFO breadcrumb rather than a warning: the empty-data path below builds a fresh cache.
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

    auto emptyLayoutInfo = VulkanDetail::MakeVkStruct<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    emptyLayoutInfo.bindingCount = 0;
    emptyLayoutInfo.pBindings = nullptr;
    res = vkCreateDescriptorSetLayout(m_context.device, &emptyLayoutInfo, m_context.allocationCallbacks, &m_context.emptyDescriptorSetLayout);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create empty descriptor set layout. {}"), ResultToString(res));
        m_context.emptyDescriptorSetLayout = VK_NULL_HANDLE;
    }

    if(desc.graphicsQueue && desc.graphicsQueueIndex >= 0){
        m_queues[static_cast<u32>(CommandQueue::Graphics)].emplace(m_context, *this, CommandQueue::Graphics, desc.graphicsQueue, desc.graphicsQueueIndex);
    }
    if(desc.computeQueue && desc.computeQueueIndex >= 0){
        m_queues[static_cast<u32>(CommandQueue::Compute)].emplace(m_context, *this, CommandQueue::Compute, desc.computeQueue, desc.computeQueueIndex);
    }
    if(desc.transferQueue && desc.transferQueueIndex >= 0){
        m_queues[static_cast<u32>(CommandQueue::Copy)].emplace(m_context, *this, CommandQueue::Copy, desc.transferQueue, desc.transferQueueIndex);
    }
}
Device::~Device(){
    waitForIdle();

    m_uploadManager.clear();
    m_scratchManager.clear();

    // Release the global descriptor heap's tables/layouts while the device is still valid (idempotent; the member
    // destructor also calls this).
    m_gpuDescriptorHeap.shutdown();
    m_descriptorHeapManager.shutdown();

    for(u32 i = 0; i < static_cast<u32>(CommandQueue::kCount); ++i)
        m_queues[i].reset();

    // Freed after the queues (and their command lists) so isAmdBreadcrumbEnabled() stays stable while command
    // lists unregister their marker trackers in their destructors.
    if(m_amdBreadcrumb.buffer != VK_NULL_HANDLE)
        m_allocator.destroyHostMappedBuffer(m_amdBreadcrumb.buffer, m_amdBreadcrumb.allocation, m_amdBreadcrumb.mappedMemory);

    if(m_context.emptyDescriptorSetLayout){
        vkDestroyDescriptorSetLayout(m_context.device, m_context.emptyDescriptorSetLayout, m_context.allocationCallbacks);
        m_context.emptyDescriptorSetLayout = VK_NULL_HANDLE;
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
    if(!VulkanDetail::RuntimeCacheVolumeExists(m_pipelineCacheDirectory, m_pipelineCacheVolumeName))
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


CommandListHandle Device::createCommandList(const CommandListParameters& params){
    auto* cmdList = NewArenaObject<CommandList>(m_context.objectArena, *this, params);
    return CommandListHandle(cmdList, CommandListHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

u64 Device::executeCommandLists(CommandList* const* pCommandLists, usize numCommandLists, CommandQueue::Enum executionQueue){
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

    bool submittedWork = false;
    const u64 submittedID = queue->submit(pCommandLists, numCommandLists, &submittedWork);

    if(!submittedOwners.empty()){
        if(submittedWork){
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

bool Device::waitForIdle(){
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
    if(!m_gpuCrashDiagnosticsEnabled)
        return;

    const bool hasCheckpoints = m_context.extensions.NV_device_diagnostic_checkpoints;
    const bool hasDeviceFault = m_context.extensions.EXT_device_fault;
    const bool hasBufferMarker = m_context.extensions.AMD_buffer_marker && m_amdBreadcrumb.buffer != VK_NULL_HANDLE;

    // Claim the one-shot capture atomically: only the first thread to lose the device proceeds, so a
    // device-lost reported concurrently from submit/present/waitForIdle never dispatches two crash dumps.
    if(m_gpuCrashCaptured.exchange(true))
        return;

    GpuCrashReport report(m_gpuCrashReportArena);
    Vector<u8, Alloc::PersistentArena> vendorBinary(m_gpuCrashVendorBinaryArena);

    // The report lives in a fixed, pre-reserved arena so capture never touches the growable heap. Formatting
    // is wrapped: if the arena is exhausted the report degrades to whatever was built rather than throwing
    // std::bad_alloc out of the device-lost path (the function is noexcept, so an escape would terminate).
    try{
        report.details.reserve(s_MaxGpuCrashReportChars);
        report.context.append(context.data(), context.size());

        // A single aggregate budget across all queues AND both fault sections keeps the formatted output
        // within the fixed arena. (Clamping per-queue would let kCount queues each emit the full cap.)
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
                // Each slot holds the highest sequence the GPU executed there; the slot with the global max
                // sequence is the furthest point the GPU reached before the device was lost.
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
                        // Read the CPU-side record under the same lock reserveAmdBreadcrumb writes it, so a
                        // worker still recording at device-lost cannot hand us a torn {sequence, markerHash}.
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
                        // The CPU lapped this ring slot after the GPU wrote it, so the recorded label is stale.
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
        // Fixed crash arena exhausted while formatting; ship the partial report rather than terminate.
    }

    // Logging itself formats/allocates; never let it abort the crash path.
    try{
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: GPU crash detected during {}:\n{}"), StringConvert(report.context.c_str()), StringConvert(report.details.c_str()));
    }
    catch(...){
    }

    // Attach the NVIDIA Aftermath GPU crash dump if one was collected. WaitForCrashDump blocks briefly while
    // the driver finishes writing the dump; the bytes stay owned by the Aftermath module for the synchronous
    // dispatch below, where the crash reporter copies them into the package.
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

    // Monotonic sequence (>=1 so a zeroed slot reads as "never reached"), ring-mapped to a slot. The CPU-side
    // record lets device-lost readback map the furthest-reached slot back to its marker hash.
    const u32 sequence = m_amdBreadcrumb.nextSequence.fetch_add(1u) + 1u;
    const u32 slot = sequence % s_MaxAmdBreadcrumbSlots;
    {
        // Concurrent recording can land two reservations on the same ring slot; serialize the pair store so
        // device-lost readback never sees a torn {sequence, markerHash}. beginMarker is a region-boundary op,
        // so this brief lock is not on a hot per-draw path.
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
        return m_context.extensions.KHR_acceleration_structure;
    case Feature::RayTracingPipeline:
        return m_context.extensions.KHR_ray_tracing_pipeline;
    case Feature::RayQuery:
        return m_context.extensions.KHR_ray_query;
    case Feature::ShaderExecutionReordering:
        return m_context.extensions.EXT_ray_tracing_invocation_reorder || m_context.extensions.NV_ray_tracing_invocation_reorder;
    case Feature::Spheres:
        return m_context.extensions.NV_ray_tracing_linear_swept_spheres && m_context.rayTracingLinearSweptSpheresFeatures.spheres == VK_TRUE;
    case Feature::LinearSweptSpheres:
        return m_context.extensions.NV_ray_tracing_linear_swept_spheres && m_context.rayTracingLinearSweptSpheresFeatures.linearSweptSpheres == VK_TRUE;
    case Feature::RayTracingOpacityMicromap:
        return m_context.extensions.EXT_opacity_micromap && m_context.extensions.KHR_synchronization2;
    case Feature::RayTracingClusters:
        return m_context.extensions.NV_cluster_acceleration_structure;
    case Feature::CooperativeVectorInferencing:
        return m_context.extensions.NV_cooperative_vector && m_context.coopVecFeatures.cooperativeVector;
    case Feature::CooperativeVectorTraining:
        return m_context.extensions.NV_cooperative_vector && m_context.coopVecFeatures.cooperativeVectorTraining;
    case Feature::Meshlets:
        return m_context.extensions.EXT_mesh_shader && m_context.meshShaderFeatures.meshShader == VK_TRUE && vkCmdDrawMeshTasksEXT;
    case Feature::VariableRateShading:
        return m_context.extensions.KHR_fragment_shading_rate;
    case Feature::WaveLaneCountMinMax:{
        // Wave/subgroup size is core Vulkan 1.1 (engine floor is 1.3), so this is always supported.
        auto* out = static_cast<WaveLaneCountMinMaxFeatureInfo*>(featureInfo);
        if(out && featureInfoSize >= sizeof(WaveLaneCountMinMaxFeatureInfo)){
            out->minWaveLaneCount = m_context.subgroupProperties.subgroupSize;
            out->maxWaveLaneCount = m_context.subgroupProperties.subgroupSize;
        }
        return true;
    }
    case Feature::SamplerFeedback:
        return false;
    case Feature::VirtualResources:
        return false;
    case Feature::ComputeQueue:
        return m_queues[static_cast<u32>(CommandQueue::Compute)].has_value();
    case Feature::CopyQueue:
        return m_queues[static_cast<u32>(CommandQueue::Copy)].has_value();
    case Feature::ConstantBufferRanges:
        return true;
    default:
        return false;
    }
}

FormatSupport::Mask Device::queryFormatSupport(Format::Enum format){
    VkFormat vkFormat = ConvertFormat(format);
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

