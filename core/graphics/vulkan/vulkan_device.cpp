// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <core/filesystem/filesystem.h>
#include <core/filesystem/volume_naming.h>
#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_PipelineCacheVirtualPath = "vulkan/pipeline_cache.bin";
static constexpr u64 s_PipelineCacheVolumeSegmentSize = 16ull * 1024ull * 1024ull;
static constexpr u64 s_PipelineCacheVolumeMetadataSize = 4ull * 1024ull;
static constexpr usize s_PipelineCacheDataMaxAttempts = 4;


template<typename T>
static u64 UpdateFnv64Value(const u64 hash, const T& value){
    return UpdateFnv64(hash, reinterpret_cast<const u8*>(&value), sizeof(T));
}

static u64 ComputePipelineCacheIdentityHash(const VkPhysicalDeviceProperties& properties){
    u64 hash = FNV64_OFFSET_BASIS;
    static constexpr AStringView s_Tag = "nwb.vulkan.pipeline_cache.v1";
    hash = UpdateFnv64(hash, reinterpret_cast<const u8*>(s_Tag.data()), s_Tag.size());
    hash = UpdateFnv64Value(hash, properties.apiVersion);
    hash = UpdateFnv64Value(hash, properties.driverVersion);
    hash = UpdateFnv64Value(hash, properties.vendorID);
    hash = UpdateFnv64Value(hash, properties.deviceID);
    hash = UpdateFnv64(hash, properties.pipelineCacheUUID, VK_UUID_SIZE);
    return hash;
}

static AString MakePipelineCacheVolumeName(const VkPhysicalDeviceProperties& properties){
    return StringFormat("runtime_cache_{}", FormatHex64(ComputePipelineCacheIdentityHash(properties)));
}

static bool RuntimeCacheVolumeExists(const Path& directory, const AStringView volumeName){
    if(directory.empty())
        return false;

    ErrorCode errorCode;
    if(!IsDirectory(directory, errorCode) || errorCode)
        return false;

    const Path segmentPath = directory / Filesystem::MakeVolumeSegmentFileName(volumeName, 0);
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
    Filesystem::VolumeMountDesc mountDesc;
    mountDesc.volumeName = AString(volumeName);
    mountDesc.mountDirectory = directory;
    mountDesc.createIfMissing = createIfMissing;
    mountDesc.usage = usage;
    if(createIfMissing){
        mountDesc.segmentSize = s_PipelineCacheVolumeSegmentSize;
        mountDesc.metadataSize = s_PipelineCacheVolumeMetadataSize;
    }

    return outVolume.mount(mountDesc);
}

static bool ValidatePipelineCacheData(const Vector<u8>& cacheData, const VkPhysicalDeviceProperties& properties){
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

static bool RetrievePipelineCacheData(VkDevice device, VkPipelineCache pipelineCache, Vector<u8>& outData){
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
            NWB_LOGGER_WARNING(
                NWB_TEXT("Vulkan: Pipeline cache data size {} exceeds runtime buffer limit {}."),
                static_cast<u64>(cacheSize),
                static_cast<u64>(Limit<usize>::s_Max)
            );
            return false;
        }

        Vector<u8> cacheData(static_cast<usize>(cacheSize), 0);
        size_t retrievedSize = cacheSize;
        res = vkGetPipelineCacheData(device, pipelineCache, &retrievedSize, cacheData.data());
        if(res == VK_SUCCESS){
            if(retrievedSize > cacheSize || retrievedSize > static_cast<size_t>(Limit<usize>::s_Max)){
                NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Driver returned an invalid pipeline cache data size while serializing."));
                return false;
            }

            cacheData.resize(static_cast<usize>(retrievedSize));
            outData = Move(cacheData);
            return true;
        }
        if(res == VK_INCOMPLETE)
            continue;

        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to retrieve pipeline cache data. {}"), ResultToString(res));
        return false;
    }

    NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Pipeline cache data kept changing while serializing."));
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static SystemMemoryAllocationScope::Enum ConvertAllocationScope(VkSystemAllocationScope scope){
    switch(scope){
    case VK_SYSTEM_ALLOCATION_SCOPE_COMMAND:
        return SystemMemoryAllocationScope::Command;
    case VK_SYSTEM_ALLOCATION_SCOPE_OBJECT:
        return SystemMemoryAllocationScope::Object;
    case VK_SYSTEM_ALLOCATION_SCOPE_CACHE:
        return SystemMemoryAllocationScope::Cache;
    case VK_SYSTEM_ALLOCATION_SCOPE_DEVICE:
        return SystemMemoryAllocationScope::Device;
    case VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE:
        return SystemMemoryAllocationScope::Instance;
    default:
        return SystemMemoryAllocationScope::Object;
    }
}

static void* VulkanSystemAllocation(void* pUserData, size_t size, size_t alignment, VkSystemAllocationScope allocationScope){
    auto* allocator = static_cast<SystemMemoryAllocator*>(pUserData);
    if(!allocator || !allocator->allocate)
        return nullptr;

    return allocator->allocate(
        allocator->userData,
        static_cast<usize>(size),
        static_cast<usize>(alignment),
        ConvertAllocationScope(allocationScope)
    );
}

static void* VulkanSystemReallocation(void* pUserData, void* pOriginal, size_t size, size_t alignment, VkSystemAllocationScope allocationScope){
    auto* allocator = static_cast<SystemMemoryAllocator*>(pUserData);
    if(!allocator || !allocator->reallocate)
        return nullptr;

    return allocator->reallocate(
        allocator->userData,
        pOriginal,
        static_cast<usize>(size),
        static_cast<usize>(alignment),
        ConvertAllocationScope(allocationScope)
    );
}

static void VulkanSystemFree(void* pUserData, void* pMemory){
    auto* allocator = static_cast<SystemMemoryAllocator*>(pUserData);
    if(!allocator || !allocator->deallocate)
        return;

    allocator->deallocate(allocator->userData, pMemory);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Device::Device(const DeviceDesc& desc)
    : RefCounter<IDevice>(desc.threadPool)
    , m_aftermathEnabled(desc.aftermathEnabled)
    , m_context(desc.allocator, desc.threadPool, desc.instance, desc.physicalDevice, desc.device, desc.allocationCallbacks)
    , m_allocator(m_context)
    , m_descriptorHeapManager(m_context)
    , m_pipelineCacheDirectory(desc.pipelineCacheDirectory)
{
    VkResult res = VK_SUCCESS;

    m_context.descriptorHeapManager = &m_descriptorHeapManager;

    if(!desc.allocationCallbacks && desc.systemMemoryAllocator && desc.systemMemoryAllocator->valid()){
        m_allocationCallbacksStorage = {};
        m_allocationCallbacksStorage.pUserData = const_cast<SystemMemoryAllocator*>(desc.systemMemoryAllocator);
        m_allocationCallbacksStorage.pfnAllocation = VulkanDetail::VulkanSystemAllocation;
        m_allocationCallbacksStorage.pfnReallocation = VulkanDetail::VulkanSystemReallocation;
        m_allocationCallbacksStorage.pfnFree = VulkanDetail::VulkanSystemFree;
        m_allocationCallbacksStorage.pfnInternalAllocation = nullptr;
        m_allocationCallbacksStorage.pfnInternalFree = nullptr;
        m_context.allocationCallbacks = &m_allocationCallbacksStorage;
    }

    vkGetPhysicalDeviceProperties(m_context.physicalDevice, &m_context.physicalDeviceProperties);
    vkGetPhysicalDeviceMemoryProperties(m_context.physicalDevice, &m_context.memoryProperties);
    m_pipelineCacheVolumeName = VulkanDetail::MakePipelineCacheVolumeName(m_context.physicalDeviceProperties);

    m_context.extensions.buffer_device_address = desc.bufferDeviceAddressSupported;
    m_context.extensions.KHR_dynamic_rendering = desc.dynamicRenderingSupported;
    m_context.extensions.KHR_synchronization2 = desc.synchronization2Supported;

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
        else if(NWB_STRCMP(ext, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0)
            m_context.extensions.EXT_debug_utils = true;
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
        else if(NWB_STRCMP(ext, VK_EXT_MESH_SHADER_EXTENSION_NAME) == 0)
            m_context.extensions.EXT_mesh_shader = true;
        else if(NWB_STRCMP(ext, VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME) == 0)
            m_context.extensions.KHR_fragment_shading_rate = true;
        else if(NWB_STRCMP(ext, VK_NV_RAY_TRACING_INVOCATION_REORDER_EXTENSION_NAME) == 0)
            m_context.extensions.NV_ray_tracing_invocation_reorder = true;
    }

    {
        VkPhysicalDeviceProperties2 props2 = VulkanDetail::MakeVkStruct<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
        void* pNext = nullptr;

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
        VkPhysicalDeviceFeatures2 features2 = VulkanDetail::MakeVkStruct<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
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
        )
        {
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Descriptor heap entry points are unavailable, falling back to descriptor sets."));
            m_context.extensions.EXT_descriptor_heap = false;
        }
    }

    if(m_context.extensions.EXT_descriptor_heap){
        VkPhysicalDeviceProperties2 props2 = VulkanDetail::MakeVkStruct<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
        m_context.descriptorHeapProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_HEAP_PROPERTIES_EXT;
        props2.pNext = &m_context.descriptorHeapProperties;
        vkGetPhysicalDeviceProperties2(m_context.physicalDevice, &props2);

        if(!m_descriptorHeapManager.initialize()){
            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Descriptor heap initialization failed, falling back to descriptor sets."));
            m_context.extensions.EXT_descriptor_heap = false;
        }
    }

    Vector<u8> pipelineCacheInitialData;
    (void)loadPipelineCacheData(pipelineCacheInitialData);

    VkPipelineCacheCreateInfo cacheInfo = VulkanDetail::MakeVkStruct<VkPipelineCacheCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO);
    if(!pipelineCacheInitialData.empty()){
        cacheInfo.initialDataSize = pipelineCacheInitialData.size();
        cacheInfo.pInitialData = pipelineCacheInitialData.data();
    }
    res = vkCreatePipelineCache(m_context.device, &cacheInfo, m_context.allocationCallbacks, &m_context.pipelineCache);
    if(res != VK_SUCCESS && !pipelineCacheInitialData.empty()){
        NWB_LOGGER_WARNING(
            NWB_TEXT("Vulkan: Failed to create pipeline cache from runtime volume '{}'. Retrying empty cache. {}"),
            StringConvert(m_pipelineCacheVolumeName),
            ResultToString(res)
        );
        cacheInfo.initialDataSize = 0;
        cacheInfo.pInitialData = nullptr;
        res = vkCreatePipelineCache(m_context.device, &cacheInfo, m_context.allocationCallbacks, &m_context.pipelineCache);
    }
    if(res != VK_SUCCESS){
        m_context.pipelineCache = VK_NULL_HANDLE;
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to create pipeline cache. {}"), ResultToString(res));
    }

    VkDescriptorSetLayoutCreateInfo emptyLayoutInfo = VulkanDetail::MakeVkStruct<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    emptyLayoutInfo.bindingCount = 0;
    emptyLayoutInfo.pBindings = nullptr;
    res = vkCreateDescriptorSetLayout(m_context.device, &emptyLayoutInfo, m_context.allocationCallbacks, &m_context.emptyDescriptorSetLayout);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create empty descriptor set layout. {}"), ResultToString(res));
        m_context.emptyDescriptorSetLayout = VK_NULL_HANDLE;
    }

    if(desc.graphicsQueue && desc.graphicsQueueIndex >= 0){
        auto& arena = m_context.objectArena;
        auto* mem = arena.allocate<Queue>(1);
        auto* q = new(mem) Queue(m_context, CommandQueue::Graphics, desc.graphicsQueue, desc.graphicsQueueIndex);
        m_queues[static_cast<u32>(CommandQueue::Graphics)] = CustomUniquePtr<Queue>(q, CustomUniquePtr<Queue>::deleter_type(arena));
    }
    if(desc.computeQueue && desc.computeQueueIndex >= 0){
        auto& arena = m_context.objectArena;
        auto* mem = arena.allocate<Queue>(1);
        auto* q = new(mem) Queue(m_context, CommandQueue::Compute, desc.computeQueue, desc.computeQueueIndex);
        m_queues[static_cast<u32>(CommandQueue::Compute)] = CustomUniquePtr<Queue>(q, CustomUniquePtr<Queue>::deleter_type(arena));
    }
    if(desc.transferQueue && desc.transferQueueIndex >= 0){
        auto& arena = m_context.objectArena;
        auto* mem = arena.allocate<Queue>(1);
        auto* q = new(mem) Queue(m_context, CommandQueue::Copy, desc.transferQueue, desc.transferQueueIndex);
        m_queues[static_cast<u32>(CommandQueue::Copy)] = CustomUniquePtr<Queue>(q, CustomUniquePtr<Queue>::deleter_type(arena));
    }

    {
        auto& arena = m_context.objectArena;
        auto* mem = arena.allocate<UploadManager>(1);
        auto* p = new(mem) UploadManager(*this, s_DefaultUploadChunkSize, 0, false);
        m_uploadManager = CustomUniquePtr<UploadManager>(p, CustomUniquePtr<UploadManager>::deleter_type(arena));
    }
    {
        auto& arena = m_context.objectArena;
        auto* mem = arena.allocate<UploadManager>(1);
        auto* p = new(mem) UploadManager(*this, s_DefaultScratchChunkSize, s_ScratchMemoryLimit, true);
        m_scratchManager = CustomUniquePtr<UploadManager>(p, CustomUniquePtr<UploadManager>::deleter_type(arena));
    }
}
Device::~Device(){
    waitForIdle();

    m_uploadManager.reset();
    m_scratchManager.reset();

    m_descriptorHeapManager.shutdown();

    for(u32 i = 0; i < static_cast<u32>(CommandQueue::kCount); ++i)
        m_queues[i].reset();

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

bool Device::loadPipelineCacheData(Vector<u8>& outData){
    outData.clear();
    if(m_pipelineCacheDirectory.empty() || m_pipelineCacheVolumeName.empty())
        return false;
    if(!VulkanDetail::RuntimeCacheVolumeExists(m_pipelineCacheDirectory, m_pipelineCacheVolumeName))
        return false;

    Filesystem::VolumeFileSystem volume(m_context.objectArena);
    if(!VulkanDetail::MountPipelineCacheVolume(
        m_pipelineCacheDirectory,
        m_pipelineCacheVolumeName,
        false,
        Filesystem::VolumeUsage::RuntimeReadOnly,
        volume
    )){
        NWB_LOGGER_WARNING(
            NWB_TEXT("Vulkan: Failed to mount pipeline cache runtime volume '{}' from '{}'."),
            StringConvert(m_pipelineCacheVolumeName),
            PathToString<tchar>(m_pipelineCacheDirectory)
        );
        return false;
    }

    const Name cachePath(VulkanDetail::s_PipelineCacheVirtualPath);
    if(!volume.fileExists(cachePath))
        return false;
    if(!volume.readFile(cachePath, outData)){
        outData.clear();
        NWB_LOGGER_WARNING(
            NWB_TEXT("Vulkan: Failed to read pipeline cache data from runtime volume '{}'."),
            StringConvert(m_pipelineCacheVolumeName)
        );
        return false;
    }
    if(!VulkanDetail::ValidatePipelineCacheData(outData, m_context.physicalDeviceProperties)){
        outData.clear();
        NWB_LOGGER_WARNING(
            NWB_TEXT("Vulkan: Ignoring incompatible pipeline cache data in runtime volume '{}'."),
            StringConvert(m_pipelineCacheVolumeName)
        );
        return false;
    }

    NWB_LOGGER_INFO(
        NWB_TEXT("Vulkan: Loaded pipeline cache runtime volume '{}' ({} bytes)."),
        StringConvert(m_pipelineCacheVolumeName),
        outData.size()
    );
    return true;
}

void Device::savePipelineCacheData(){
    if(m_pipelineCacheDirectory.empty() || m_pipelineCacheVolumeName.empty() || !m_context.pipelineCache)
        return;

    Vector<u8> cacheData;
    if(!VulkanDetail::RetrievePipelineCacheData(m_context.device, m_context.pipelineCache, cacheData))
        return;
    if(cacheData.empty())
        return;

    if(!VulkanDetail::ValidatePipelineCacheData(cacheData, m_context.physicalDeviceProperties)){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Driver returned incompatible pipeline cache data; skipping runtime cache write."));
        return;
    }

    Filesystem::VolumeFileSystem volume(m_context.objectArena);
    if(!VulkanDetail::MountPipelineCacheVolume(
        m_pipelineCacheDirectory,
        m_pipelineCacheVolumeName,
        true,
        Filesystem::VolumeUsage::RuntimeReadWrite,
        volume
    )){
        NWB_LOGGER_WARNING(
            NWB_TEXT("Vulkan: Failed to mount pipeline cache runtime volume '{}' for write at '{}'."),
            StringConvert(m_pipelineCacheVolumeName),
            PathToString<tchar>(m_pipelineCacheDirectory)
        );
        if(!Filesystem::RemoveVolumeSegments(m_pipelineCacheDirectory, m_pipelineCacheVolumeName)){
            NWB_LOGGER_WARNING(
                NWB_TEXT("Vulkan: Failed to remove unusable pipeline cache runtime volume '{}'."),
                StringConvert(m_pipelineCacheVolumeName)
            );
            return;
        }
        if(!VulkanDetail::MountPipelineCacheVolume(
            m_pipelineCacheDirectory,
            m_pipelineCacheVolumeName,
            true,
            Filesystem::VolumeUsage::RuntimeReadWrite,
            volume
        )){
            NWB_LOGGER_WARNING(
                NWB_TEXT("Vulkan: Failed to recreate pipeline cache runtime volume '{}'."),
                StringConvert(m_pipelineCacheVolumeName)
            );
            return;
        }
    }

    const Name cachePath(VulkanDetail::s_PipelineCacheVirtualPath);
    if(!volume.writeFile(cachePath, cacheData)){
        NWB_LOGGER_WARNING(
            NWB_TEXT("Vulkan: Failed to write pipeline cache data to runtime volume '{}'."),
            StringConvert(m_pipelineCacheVolumeName)
        );
        return;
    }
    if(!volume.compact(true)){
        NWB_LOGGER_WARNING(
            NWB_TEXT("Vulkan: Failed to compact pipeline cache runtime volume '{}'."),
            StringConvert(m_pipelineCacheVolumeName)
        );
    }

    NWB_LOGGER_INFO(
        NWB_TEXT("Vulkan: Saved pipeline cache runtime volume '{}' ({} bytes)."),
        StringConvert(m_pipelineCacheVolumeName),
        cacheData.size()
    );
}

Queue* Device::getQueue(CommandQueue::Enum queueType)const{
    auto index = static_cast<u32>(queueType);
    if(index < static_cast<u32>(CommandQueue::kCount)){
        return m_queues[index].get();
    }
    return nullptr;
}


CommandListHandle Device::createCommandList(const CommandListParameters& params){
    auto* cmdList = NewArenaObject<CommandList>(m_context.objectArena, *this, params);
    return CommandListHandle(cmdList, CommandListHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

u64 Device::executeCommandLists(ICommandList* const* pCommandLists, usize numCommandLists, CommandQueue::Enum executionQueue){
    Queue* queue = getQueue(executionQueue);
    if(!queue){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to execute command lists: requested queue is not available"));
        return 0;
    }

    Alloc::ScratchArena<> scratchArena;
    Vector<TrackedCommandBuffer*, Alloc::ScratchAllocator<TrackedCommandBuffer*>> submittedOwners{ Alloc::ScratchAllocator<TrackedCommandBuffer*>(scratchArena) };
    if(pCommandLists && numCommandLists > 0){
        submittedOwners.reserve(numCommandLists);
        for(usize i = 0; i < numCommandLists; ++i){
            if(!pCommandLists[i])
                continue;
            auto* cmdList = checked_cast<CommandList*>(pCommandLists[i]);
            if(cmdList && cmdList->m_currentCmdBuf)
                submittedOwners.push_back(cmdList->m_currentCmdBuf.get());
        }
    }

    bool submittedWork = false;
    const u64 submittedID = queue->submit(pCommandLists, numCommandLists, &submittedWork);

    if(!submittedOwners.empty()){
        if(submittedWork){
            if(m_uploadManager)
                m_uploadManager->submitChunks(executionQueue, submittedID, submittedOwners.data(), submittedOwners.size());
            if(m_scratchManager)
                m_scratchManager->submitChunks(executionQueue, submittedID, submittedOwners.data(), submittedOwners.size());
        }
        else{
            const auto ownerStillRecorded = [&](TrackedCommandBuffer* owner) -> bool {
                if(!owner || !pCommandLists)
                    return false;
                for(usize i = 0; i < numCommandLists; ++i){
                    auto* cmdList = checked_cast<CommandList*>(pCommandLists[i]);
                    if(cmdList && cmdList->m_currentCmdBuf.get() == owner)
                        return true;
                }
                return false;
            };

            const u64 reusableVersion = queueGetCompletedInstance(executionQueue);
            for(TrackedCommandBuffer* owner : submittedOwners){
                if(ownerStillRecorded(owner))
                    continue;
                if(m_uploadManager)
                    m_uploadManager->discardChunks(executionQueue, owner, reusableVersion);
                if(m_scratchManager)
                    m_scratchManager->discardChunks(executionQueue, owner, reusableVersion);
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
        return false;
    }
    else if(res != VK_SUCCESS){
        NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: Failed to wait for device idle. {}"), ResultToString(res));
        return false;
    }

    for(u32 i = 0; i < static_cast<u32>(CommandQueue::kCount); ++i){
        if(m_queues[i]){
            ScopedLock lock(m_queues[i]->m_mutex);
            m_queues[i]->updateLastFinishedID();
        }
    }

    return true;
}

void Device::runGarbageCollection(){
    for(u32 i = 0; i < static_cast<u32>(CommandQueue::kCount); ++i){
        if(m_queues[i]){
            ScopedLock lock(m_queues[i]->m_mutex);
            m_queues[i]->updateLastFinishedID();
        }
    }
}


bool Device::queryFeatureSupport(Feature::Enum feature, void*, usize){
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
        return m_context.extensions.NV_ray_tracing_invocation_reorder;
    case Feature::RayTracingOpacityMicromap:
        return m_context.extensions.EXT_opacity_micromap && m_context.extensions.KHR_synchronization2;
    case Feature::RayTracingClusters:
        return m_context.extensions.NV_cluster_acceleration_structure;
    case Feature::CooperativeVectorInferencing:
        return m_context.extensions.NV_cooperative_vector && m_context.coopVecFeatures.cooperativeVector;
    case Feature::CooperativeVectorTraining:
        return m_context.extensions.NV_cooperative_vector && m_context.coopVecFeatures.cooperativeVectorTraining;
    case Feature::Meshlets:
        return m_context.extensions.EXT_mesh_shader && vkCmdDrawMeshTasksEXT;
    case Feature::VariableRateShading:
        return m_context.extensions.KHR_fragment_shading_rate;
    case Feature::SamplerFeedback:
        return false;
    case Feature::VirtualResources:
        return false;
    case Feature::ComputeQueue:
        return m_queues[static_cast<u32>(CommandQueue::Compute)] != nullptr;
    case Feature::CopyQueue:
        return m_queues[static_cast<u32>(CommandQueue::Copy)] != nullptr;
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


Heap::Heap(const VulkanContext& context)
    : RefCounter<IHeap>(context.threadPool)
    , m_context(context)
{}
Heap::~Heap(){
    if(m_memory != VK_NULL_HANDLE){
        vkFreeMemory(m_context.device, m_memory, m_context.allocationCallbacks);
        m_memory = VK_NULL_HANDLE;
    }
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

    VkMemoryPropertyFlags memoryProperties = 0;
    bool isReadbackHeap = false;
    switch(d.type){
        case HeapType::DeviceLocal:
            memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            break;
        case HeapType::Upload:
            memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            break;
        case HeapType::Readback:
            memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
            isReadbackHeap = true;
            break;
        default:
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create heap: invalid heap type"));
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create heap: invalid heap type"));
            return nullptr;
    }

    u32 memoryTypeIndex = UINT32_MAX;
    for(u32 i = 0; i < m_context.memoryProperties.memoryTypeCount; ++i){
        if((m_context.memoryProperties.memoryTypes[i].propertyFlags & memoryProperties) == memoryProperties){
            memoryTypeIndex = i;
            break;
        }
    }

    if(memoryTypeIndex == UINT32_MAX && isReadbackHeap){
        memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for(u32 i = 0; i < m_context.memoryProperties.memoryTypeCount; ++i){
            if((m_context.memoryProperties.memoryTypes[i].propertyFlags & memoryProperties) == memoryProperties){
                memoryTypeIndex = i;
                break;
            }
        }
    }

    if(memoryTypeIndex == UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to find suitable memory type for heap"));
        return nullptr;
    }

    auto* heap = NewArenaObject<Heap>(m_context.objectArena, m_context);
    heap->m_desc = d;
    heap->m_memoryTypeIndex = memoryTypeIndex;

    void* pNext = nullptr;
    VkMemoryAllocateFlagsInfo allocFlagsInfo{};
    if(m_context.extensions.buffer_device_address){
        allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        allocFlagsInfo.pNext = pNext;
        pNext = &allocFlagsInfo;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = d.capacity;
    allocInfo.memoryTypeIndex = memoryTypeIndex;
    allocInfo.pNext = pNext;

    res = vkAllocateMemory(m_context.device, &allocInfo, m_context.allocationCallbacks, &heap->m_memory);
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

    CooperativeVectorDeviceFeatures output;

    if(!m_context.extensions.NV_cooperative_vector || !m_context.coopVecFeatures.cooperativeVector)
        return output;

    uint32_t propertyCount = 0;
    res = vkGetPhysicalDeviceCooperativeVectorPropertiesNV(m_context.physicalDevice, &propertyCount, nullptr);
    if(res != VK_SUCCESS || propertyCount == 0)
        return output;

    Alloc::ScratchArena<> scratchArena;
    Vector<VkCooperativeVectorPropertiesNV, Alloc::ScratchAllocator<VkCooperativeVectorPropertiesNV>> properties(propertyCount, Alloc::ScratchAllocator<VkCooperativeVectorPropertiesNV>(scratchArena));
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

    output.trainingFloat16 = m_context.coopVecFeatures.cooperativeVectorTraining != VK_FALSE
        && m_context.coopVecProperties.cooperativeVectorTrainingFloat16Accumulation != VK_FALSE;
    output.trainingFloat32 = m_context.coopVecFeatures.cooperativeVectorTraining != VK_FALSE
        && m_context.coopVecProperties.cooperativeVectorTrainingFloat32Accumulation != VK_FALSE;

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

    VkConvertCooperativeVectorMatrixInfoNV convertInfo = VulkanDetail::MakeVkStruct<VkConvertCooperativeVectorMatrixInfoNV>(VK_STRUCTURE_TYPE_CONVERT_COOPERATIVE_VECTOR_MATRIX_INFO_NV);
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

AftermathCrashDumpHelper& Device::getAftermathCrashDumpHelper(){
    return m_aftermathCrashDumpHelper;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DeviceHandle CreateDevice(const DeviceDesc& desc){
    auto* device = NewArenaObject<Device>(desc.allocator.getObjectArena(), desc);
    return DeviceHandle(device, DeviceHandle::deleter_type(&desc.allocator.getObjectArena()), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

