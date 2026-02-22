// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vulkan{


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
    : m_aftermathEnabled(desc.aftermathEnabled)
    , m_allocator(m_context)
{
    VkResult res = VK_SUCCESS;

    m_context.instance = desc.instance;
    m_context.physicalDevice = desc.physicalDevice;
    m_context.device = desc.device;
    m_context.allocator = desc.allocator;
    m_context.objectArena = desc.allocator ? &desc.allocator->getObjectArena() : nullptr;

    if(desc.allocationCallbacks)
        m_context.allocationCallbacks = desc.allocationCallbacks;
    else if(desc.systemMemoryAllocator && desc.systemMemoryAllocator->isValid()){
        m_allocationCallbacksStorage = {};
        m_allocationCallbacksStorage.pUserData = const_cast<SystemMemoryAllocator*>(desc.systemMemoryAllocator);
        m_allocationCallbacksStorage.pfnAllocation = __hidden_vulkan::VulkanSystemAllocation;
        m_allocationCallbacksStorage.pfnReallocation = __hidden_vulkan::VulkanSystemReallocation;
        m_allocationCallbacksStorage.pfnFree = __hidden_vulkan::VulkanSystemFree;
        m_allocationCallbacksStorage.pfnInternalAllocation = nullptr;
        m_allocationCallbacksStorage.pfnInternalFree = nullptr;
        m_context.allocationCallbacks = &m_allocationCallbacksStorage;
    }
    else
        m_context.allocationCallbacks = nullptr;

    vkGetPhysicalDeviceProperties(m_context.physicalDevice, &m_context.physicalDeviceProperties);
    vkGetPhysicalDeviceMemoryProperties(m_context.physicalDevice, &m_context.memoryProperties);

    m_context.extensions.buffer_device_address = desc.bufferDeviceAddressSupported;

    for(usize i = 0; i < desc.numDeviceExtensions; ++i){
        const char* ext = desc.deviceExtensions[i];
        if(NWB_STRCMP(ext, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME) == 0)
            m_context.extensions.KHR_synchronization2 = true;
        else if(NWB_STRCMP(ext, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) == 0)
            m_context.extensions.KHR_ray_tracing_pipeline = true;
        else if(NWB_STRCMP(ext, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) == 0)
            m_context.extensions.KHR_acceleration_structure = true;
        else if(NWB_STRCMP(ext, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0)
            m_context.extensions.EXT_debug_utils = true;
        else if(NWB_STRCMP(ext, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
            m_context.extensions.KHR_swapchain = true;
        else if(NWB_STRCMP(ext, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) == 0)
            m_context.extensions.KHR_dynamic_rendering = true;
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

    if(m_context.extensions.KHR_ray_tracing_pipeline){
        VkPhysicalDeviceProperties2 props2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        m_context.rayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;

        void* pNext = &m_context.rayTracingPipelineProperties;

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

        props2.pNext = pNext;
        vkGetPhysicalDeviceProperties2(m_context.physicalDevice, &props2);
    }

    if(m_context.extensions.NV_cooperative_vector){
        VkPhysicalDeviceFeatures2 features2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        m_context.coopVecFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_VECTOR_FEATURES_NV;
        features2.pNext = &m_context.coopVecFeatures;
        vkGetPhysicalDeviceFeatures2(m_context.physicalDevice, &features2);
    }

    VkPipelineCacheCreateInfo cacheInfo = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
    res = vkCreatePipelineCache(m_context.device, &cacheInfo, m_context.allocationCallbacks, &m_context.pipelineCache);
    if(res != VK_SUCCESS){
        m_context.pipelineCache = VK_NULL_HANDLE;
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to create pipeline cache. {}"), ResultToString(res));
    }

    VkDescriptorSetLayoutCreateInfo emptyLayoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    emptyLayoutInfo.bindingCount = 0;
    emptyLayoutInfo.pBindings = nullptr;
    res = vkCreateDescriptorSetLayout(m_context.device, &emptyLayoutInfo, m_context.allocationCallbacks, &m_context.emptyDescriptorSetLayout);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create empty descriptor set layout. {}"), ResultToString(res));
        m_context.emptyDescriptorSetLayout = VK_NULL_HANDLE;
    }

    if(desc.graphicsQueue && desc.graphicsQueueIndex >= 0){
        auto& arena = *m_context.objectArena;
        auto* mem = arena.allocate<Queue>(1);
        auto* q = new(mem) Queue(m_context, CommandQueue::Graphics, desc.graphicsQueue, desc.graphicsQueueIndex);
        m_queues[static_cast<u32>(CommandQueue::Graphics)] = CustomUniquePtr<Queue>(q, CustomUniquePtr<Queue>::deleter_type(arena));
    }
    if(desc.computeQueue && desc.computeQueueIndex >= 0){
        auto& arena = *m_context.objectArena;
        auto* mem = arena.allocate<Queue>(1);
        auto* q = new(mem) Queue(m_context, CommandQueue::Compute, desc.computeQueue, desc.computeQueueIndex);
        m_queues[static_cast<u32>(CommandQueue::Compute)] = CustomUniquePtr<Queue>(q, CustomUniquePtr<Queue>::deleter_type(arena));
    }
    if(desc.transferQueue && desc.transferQueueIndex >= 0){
        auto& arena = *m_context.objectArena;
        auto* mem = arena.allocate<Queue>(1);
        auto* q = new(mem) Queue(m_context, CommandQueue::Copy, desc.transferQueue, desc.transferQueueIndex);
        m_queues[static_cast<u32>(CommandQueue::Copy)] = CustomUniquePtr<Queue>(q, CustomUniquePtr<Queue>::deleter_type(arena));
    }

    {
        auto& arena = *m_context.objectArena;
        auto* mem = arena.allocate<UploadManager>(1);
        auto* p = new(mem) UploadManager(*this, s_DefaultUploadChunkSize, 0, false);
        m_uploadManager = CustomUniquePtr<UploadManager>(p, CustomUniquePtr<UploadManager>::deleter_type(arena));
    }
    {
        auto& arena = *m_context.objectArena;
        auto* mem = arena.allocate<UploadManager>(1);
        auto* p = new(mem) UploadManager(*this, s_DefaultScratchChunkSize, s_ScratchMemoryLimit, true);
        m_scratchManager = CustomUniquePtr<UploadManager>(p, CustomUniquePtr<UploadManager>::deleter_type(arena));
    }
}
Device::~Device(){
    waitForIdle();

    m_uploadManager.reset();
    m_scratchManager.reset();

    for(u32 i = 0; i < static_cast<u32>(CommandQueue::kCount); ++i)
        m_queues[i].reset();

    if(m_context.emptyDescriptorSetLayout){
        vkDestroyDescriptorSetLayout(m_context.device, m_context.emptyDescriptorSetLayout, m_context.allocationCallbacks);
        m_context.emptyDescriptorSetLayout = VK_NULL_HANDLE;
    }

    if(m_context.pipelineCache){
        vkDestroyPipelineCache(m_context.device, m_context.pipelineCache, m_context.allocationCallbacks);
        m_context.pipelineCache = VK_NULL_HANDLE;
    }
}

Queue* Device::getQueue(CommandQueue::Enum queueType)const{
    auto index = static_cast<u32>(queueType);
    if(index < static_cast<u32>(CommandQueue::kCount)){
        return m_queues[index].get();
    }
    return nullptr;
}


CommandListHandle Device::createCommandList(const CommandListParameters& params){
    auto* cmdList = NewArenaObject<CommandList>(*m_context.objectArena, *this, params);
    return CommandListHandle(cmdList, CommandListHandle::deleter_type(m_context.objectArena), AdoptRef);
}

u64 Device::executeCommandLists(ICommandList* const* pCommandLists, usize numCommandLists, CommandQueue::Enum executionQueue){
    Queue* queue = getQueue(executionQueue);
    if(!queue)
        return 0;

    return queue->submit(pCommandLists, numCommandLists);
}

bool Device::waitForIdle(){
    VkResult res = VK_SUCCESS;

    res = vkDeviceWaitIdle(m_context.device);
    if(res == VK_ERROR_DEVICE_LOST){
        NWB_LOGGER_INFO(NWB_TEXT("Vulkan: Device was lost during waitForIdle."));
        return false;
    }
    else if(res != VK_SUCCESS){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to wait for device idle. {}"), ResultToString(res));
        return false;
    }

    for(u32 i = 0; i < static_cast<u32>(CommandQueue::kCount); ++i){
        if(m_queues[i]){
            m_queues[i]->updateLastFinishedID();
        }
    }

    return true;
}

void Device::runGarbageCollection(){
    for(u32 i = 0; i < static_cast<u32>(CommandQueue::kCount); ++i){
        if(m_queues[i]){
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
        return m_context.extensions.KHR_ray_tracing_pipeline;
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
        return m_context.extensions.EXT_mesh_shader;
    case Feature::VariableRateShading:
        return m_context.extensions.KHR_fragment_shading_rate;
    case Feature::VirtualResources:
        return true;
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
        return q ? Object(q->getVkQueue()) : Object(nullptr);
    }
    return Object(nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Heap::Heap(const VulkanContext& context)
    : m_context(context)
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

    auto* heap = NewArenaObject<Heap>(*m_context.objectArena, m_context);
    heap->m_desc = d;

    VkMemoryPropertyFlags memoryProperties = 0;
    switch(d.type){
        case HeapType::DeviceLocal:
            memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            break;
        case HeapType::Upload:
            memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            break;
        case HeapType::Readback:
            memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
            break;
    }

    u32 memoryTypeIndex = UINT32_MAX;
    for(u32 i = 0; i < m_context.memoryProperties.memoryTypeCount; ++i){
        if((m_context.memoryProperties.memoryTypes[i].propertyFlags & memoryProperties) == memoryProperties){
            memoryTypeIndex = i;
            break;
        }
    }

    if(memoryTypeIndex == UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to find suitable memory type for heap"));
        DestroyArenaObject(*m_context.objectArena, heap);
        return nullptr;
    }

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
        DestroyArenaObject(*m_context.objectArena, heap);
        return nullptr;
    }

    return HeapHandle(heap, HeapHandle::deleter_type(m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CooperativeVectorDeviceFeatures Device::queryCoopVecFeatures(){
    VkResult res = VK_SUCCESS;

    CooperativeVectorDeviceFeatures output;

    if(!m_context.extensions.NV_cooperative_vector)
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

    output.matMulFormats.reserve(propertyCount);
    for(const auto& prop : properties){
        CooperativeVectorMatMulFormatCombo combo;
        combo.inputType = __hidden_vulkan::ConvertCoopVecDataType(static_cast<VkComponentTypeKHR>(prop.inputType));
        combo.inputInterpretation = __hidden_vulkan::ConvertCoopVecDataType(static_cast<VkComponentTypeKHR>(prop.inputInterpretation));
        combo.matrixInterpretation = __hidden_vulkan::ConvertCoopVecDataType(static_cast<VkComponentTypeKHR>(prop.matrixInterpretation));
        combo.biasInterpretation = __hidden_vulkan::ConvertCoopVecDataType(static_cast<VkComponentTypeKHR>(prop.biasInterpretation));
        combo.outputType = __hidden_vulkan::ConvertCoopVecDataType(static_cast<VkComponentTypeKHR>(prop.resultType));
        combo.transposeSupported = prop.transpose != VK_FALSE;
        output.matMulFormats.push_back(combo);
    }

    output.trainingFloat16 = m_context.coopVecProperties.cooperativeVectorTrainingFloat16Accumulation != VK_FALSE;
    output.trainingFloat32 = m_context.coopVecProperties.cooperativeVectorTrainingFloat32Accumulation != VK_FALSE;

    return output;
}

usize Device::getCoopVecMatrixSize(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, int rows, int columns){
    VkResult res = VK_SUCCESS;

    if(!m_context.extensions.NV_cooperative_vector)
        return 0;

    usize dstSize = 0;
    usize dataTypeSize = GetCooperativeVectorDataTypeSize(type);

    VkConvertCooperativeVectorMatrixInfoNV convertInfo = { VK_STRUCTURE_TYPE_CONVERT_COOPERATIVE_VECTOR_MATRIX_INFO_NV };
    convertInfo.srcSize = dataTypeSize * rows * columns;
    convertInfo.srcData.hostAddress = nullptr;
    convertInfo.pDstSize = &dstSize;
    convertInfo.dstData.hostAddress = nullptr;
    convertInfo.srcComponentType = __hidden_vulkan::ConvertCoopVecDataType(type);
    convertInfo.dstComponentType = convertInfo.srcComponentType;
    convertInfo.numRows = static_cast<u32>(rows);
    convertInfo.numColumns = static_cast<u32>(columns);
    convertInfo.srcLayout = VK_COOPERATIVE_VECTOR_MATRIX_LAYOUT_ROW_MAJOR_NV;
    convertInfo.srcStride = dataTypeSize * columns;
    convertInfo.dstLayout = __hidden_vulkan::ConvertCoopVecMatrixLayout(layout);
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
    NWB_ASSERT(desc.allocator != nullptr);
    auto* device = NewArenaObject<Device>(desc.allocator->getObjectArena(), desc);
    return DeviceHandle(device, DeviceHandle::deleter_type(&desc.allocator->getObjectArena()), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

