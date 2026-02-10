// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Device::Device(const DeviceDesc& desc)
    : m_allocator(m_context)
{
    // Initialize Vulkan context from device description
    m_context.instance = desc.instance;
    m_context.physicalDevice = desc.physicalDevice;
    m_context.device = desc.device;
    m_context.allocationCallbacks = desc.allocationCallbacks;
    
    // Query physical device properties
    vkGetPhysicalDeviceProperties(m_context.physicalDevice, &m_context.physicalDeviceProperties);
    vkGetPhysicalDeviceMemoryProperties(m_context.physicalDevice, &m_context.memoryProperties);
    
    // Check for extensions
    m_context.extensions.buffer_device_address = desc.bufferDeviceAddressSupported;
    
    // Parse device extensions
    for(usize i = 0; i < desc.numDeviceExtensions; i++){
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
    }
    
    // Query ray tracing properties if available
    if(m_context.extensions.KHR_ray_tracing_pipeline){
        VkPhysicalDeviceProperties2 props2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        m_context.rayTracingPipelineProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
        props2.pNext = &m_context.rayTracingPipelineProperties;
        vkGetPhysicalDeviceProperties2(m_context.physicalDevice, &props2);
    }
    
    // Create pipeline cache
    VkPipelineCacheCreateInfo cacheInfo = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
    vkCreatePipelineCache(m_context.device, &cacheInfo, m_context.allocationCallbacks, &m_context.pipelineCache);
    
    // Create empty descriptor set layout
    VkDescriptorSetLayoutCreateInfo emptyLayoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    emptyLayoutInfo.bindingCount = 0;
    emptyLayoutInfo.pBindings = nullptr;
    vkCreateDescriptorSetLayout(m_context.device, &emptyLayoutInfo, m_context.allocationCallbacks, &m_context.emptyDescriptorSetLayout);
    
    // Initialize queues
    if(desc.graphicsQueue && desc.graphicsQueueIndex >= 0){
        m_queues[static_cast<u32>(CommandQueue::Graphics)] = UniquePtr<Queue>(new Queue(m_context, CommandQueue::Graphics, desc.graphicsQueue, desc.graphicsQueueIndex));
    }
    if(desc.computeQueue && desc.computeQueueIndex >= 0){
        m_queues[static_cast<u32>(CommandQueue::Compute)] = UniquePtr<Queue>(new Queue(m_context, CommandQueue::Compute, desc.computeQueue, desc.computeQueueIndex));
    }
    if(desc.transferQueue && desc.transferQueueIndex >= 0){
        m_queues[static_cast<u32>(CommandQueue::Copy)] = UniquePtr<Queue>(new Queue(m_context, CommandQueue::Copy, desc.transferQueue, desc.transferQueueIndex));
    }
    
    // Create upload managers
    constexpr u64 defaultUploadChunkSize = 64 * 1024 * 1024; // 64 MB
    constexpr u64 defaultScratchChunkSize = 16 * 1024 * 1024; // 16 MB
    constexpr u64 scratchMemoryLimit = 256 * 1024 * 1024; // 256 MB
    
    m_uploadManager = UniquePtr<UploadManager>(new UploadManager(this, defaultUploadChunkSize, 0, false));
    m_scratchManager = UniquePtr<UploadManager>(new UploadManager(this, defaultScratchChunkSize, scratchMemoryLimit, true));
}

Device::~Device(){
    // Wait for all queues to complete
    waitForIdle();
    
    // Destroy upload managers
    m_uploadManager.reset();
    m_scratchManager.reset();
    
    // Destroy queues
    for(u32 i = 0; i < static_cast<u32>(CommandQueue::kCount); i++){
        m_queues[i].reset();
    }
    
    // Destroy empty descriptor set layout
    if(m_context.emptyDescriptorSetLayout){
        vkDestroyDescriptorSetLayout(m_context.device, m_context.emptyDescriptorSetLayout, m_context.allocationCallbacks);
        m_context.emptyDescriptorSetLayout = VK_NULL_HANDLE;
    }
    
    // Destroy pipeline cache
    if(m_context.pipelineCache){
        vkDestroyPipelineCache(m_context.device, m_context.pipelineCache, m_context.allocationCallbacks);
        m_context.pipelineCache = VK_NULL_HANDLE;
    }
}

Queue* Device::getQueue(CommandQueue::Enum queueType)const{
    u32 index = static_cast<u32>(queueType);
    if(index < static_cast<u32>(CommandQueue::kCount)){
        return m_queues[index].get();
    }
    return nullptr;
}


CommandListHandle Device::createCommandList(const CommandListParameters& params){
    CommandList* cmdList = new CommandList(this, params);
    return CommandListHandle(cmdList, AdoptRef);
}

u64 Device::executeCommandLists(ICommandList* const* pCommandLists, usize numCommandLists, CommandQueue::Enum executionQueue){
    Queue* queue = getQueue(executionQueue);
    if(!queue || numCommandLists == 0)
        return 0;
    
    return queue->submit(pCommandLists, numCommandLists);
}

bool Device::waitForIdle(){
    vkDeviceWaitIdle(m_context.device);
    
    // Update all queue completion states
    for(u32 i = 0; i < static_cast<u32>(CommandQueue::kCount); i++){
        if(m_queues[i]){
            m_queues[i]->updateLastFinishedID();
        }
    }
    
    return true;
}

void Device::runGarbageCollection(){
    // Update completion status for all queues
    for(u32 i = 0; i < static_cast<u32>(CommandQueue::kCount); i++){
        if(m_queues[i]){
            m_queues[i]->updateLastFinishedID();
        }
    }
}


bool Device::queryFeatureSupport(Feature::Enum feature, void* pInfo, usize infoSize){
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
        return false; // Would need NV extension check
    case Feature::Meshlets:
        return true; // Assume VK_EXT_mesh_shader is available
    case Feature::VariableRateShading:
        return true; // Assume VK_KHR_fragment_shading_rate is available
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
    
    // Check optimal tiling features
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
    
    // Check buffer features
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
    if(memory != VK_NULL_HANDLE){
        vkFreeMemory(m_context.device, memory, m_context.allocationCallbacks);
        memory = VK_NULL_HANDLE;
    }
}

Object Heap::getNativeHandle(ObjectType objectType){
    if(objectType == ObjectTypes::VK_DeviceMemory)
        return Object(memory);
    return Object(nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


HeapHandle Device::createHeap(const HeapDesc& d){
    Heap* heap = new Heap(m_context);
    heap->desc = d;
    
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
    
    // Find suitable memory type
    u32 memoryTypeIndex = 0;
    for(u32 i = 0; i < m_context.memoryProperties.memoryTypeCount; ++i){
        if((m_context.memoryProperties.memoryTypes[i].propertyFlags & memoryProperties) == memoryProperties){
            memoryTypeIndex = i;
            break;
        }
    }
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = d.capacity;
    allocInfo.memoryTypeIndex = memoryTypeIndex;
    
    VkResult res = vkAllocateMemory(m_context.device, &allocInfo, m_context.allocationCallbacks, &heap->memory);
    if(res != VK_SUCCESS){
        delete heap;
        return nullptr;
    }
    
    return RefCountPtr<IHeap, BlankDeleter<IHeap>>(heap, AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CooperativeVectorDeviceFeatures Device::queryCoopVecFeatures(){
    // Cooperative vectors are not yet supported in this Vulkan backend
    return CooperativeVectorDeviceFeatures{};
}

usize Device::getCoopVecMatrixSize(CooperativeVectorDataType::Enum /*type*/, CooperativeVectorMatrixLayout::Enum /*layout*/, int /*rows*/, int /*columns*/){
    // Cooperative vectors are not yet supported in this Vulkan backend
    return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AftermathCrashDumpHelper& Device::getAftermathCrashDumpHelper(){
    NWB_ASSERT(!"Aftermath is not enabled in the Vulkan backend");
    // This should never be called; isAftermathEnabled() returns false.
    return *static_cast<AftermathCrashDumpHelper*>(nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DeviceHandle CreateDevice(const DeviceDesc& desc){
    Device* device = new Device(desc);
    return DeviceHandle(device, AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

