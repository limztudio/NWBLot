// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Buffer::Buffer(const VulkanContext& context, VulkanAllocator& allocator)
    : m_context(context)
    , m_allocator(allocator)
{}
Buffer::~Buffer(){
    if(managed){
        if(buffer != VK_NULL_HANDLE){
            vkDestroyBuffer(m_context.device, buffer, m_context.allocationCallbacks);
            buffer = VK_NULL_HANDLE;
        }
        
        m_allocator.freeBufferMemory(this);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


BufferHandle Device::createBuffer(const BufferDesc& d){
    auto* buffer = new Buffer(m_context, m_allocator);
    buffer->desc = d;
    
    VkBufferUsageFlags usageFlags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    
    if(d.isVertexBuffer)
        usageFlags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if(d.isIndexBuffer)
        usageFlags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if(d.isConstantBuffer)
        usageFlags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if(d.structStride != 0 || d.canHaveUAVs)
        usageFlags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if(d.isDrawIndirectArgs)
        usageFlags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if(d.isAccelStructBuildInput)
        usageFlags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    if(d.isAccelStructStorage)
        usageFlags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
    if(m_context.extensions.buffer_device_address)
        usageFlags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    
    u64 size = d.byteSize;
    
    if(d.isVolatile && d.maxVersions > 0){
        u64 alignment = m_context.physicalDeviceProperties.limits.minUniformBufferOffsetAlignment;
        size = (size + alignment - 1) & ~(alignment - 1);
        size *= d.maxVersions;
        
        buffer->versionTracking.resize(d.maxVersions);
        std::fill(buffer->versionTracking.begin(), buffer->versionTracking.end(), 0);
    }
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usageFlags;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    VkResult res = vkCreateBuffer(m_context.device, &bufferInfo, m_context.allocationCallbacks, &buffer->buffer);
    NWB_ASSERT(res == VK_SUCCESS);
    
    if(!d.isVirtual){
        VkResult res = m_allocator.allocateBufferMemory(buffer, m_context.extensions.buffer_device_address && (usageFlags & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT));
        NWB_ASSERT(res == VK_SUCCESS);
        
        if(d.isVolatile || d.cpuAccess != CpuAccessMode::None){
            VkResult res = vkMapMemory(m_context.device, buffer->memory, 0, size, 0, &buffer->mappedMemory);
            NWB_ASSERT(res == VK_SUCCESS);
        }
        
        if(m_context.extensions.buffer_device_address){
            VkBufferDeviceAddressInfo addressInfo{};
            addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            addressInfo.buffer = buffer->buffer;
            buffer->deviceAddress = vkGetBufferDeviceAddress(m_context.device, &addressInfo);
        }
    }
    
    return RefCountPtr<IBuffer, BlankDeleter<IBuffer>>(buffer, AdoptRef);
}

void* Device::mapBuffer(IBuffer* _buffer, CpuAccessMode::Enum cpuAccess){
    auto* buffer = static_cast<Buffer*>(_buffer);
    
    if(buffer->mappedMemory)
        return buffer->mappedMemory;
    
    void* data = nullptr;
    VkResult res = vkMapMemory(m_context.device, buffer->memory, 0, VK_WHOLE_SIZE, 0, &data);
    NWB_ASSERT(res == VK_SUCCESS);
    
    buffer->mappedMemory = data;
    return data;
}

void Device::unmapBuffer(IBuffer* _buffer){
    auto* buffer = static_cast<Buffer*>(_buffer);
    
    if(buffer->mappedMemory && !buffer->desc.isVolatile && buffer->desc.cpuAccess == CpuAccessMode::None){
        vkUnmapMemory(m_context.device, buffer->memory);
        buffer->mappedMemory = nullptr;
    }
}

MemoryRequirements Device::getBufferMemoryRequirements(IBuffer* _buffer){
    auto* buffer = static_cast<Buffer*>(_buffer);
    
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_context.device, buffer->buffer, &memRequirements);
    
    MemoryRequirements result;
    result.size = memRequirements.size;
    result.alignment = memRequirements.alignment;
    return result;
}

bool Device::bindBufferMemory(IBuffer* _buffer, IHeap* heap, u64 offset){
    auto* buffer = static_cast<Buffer*>(_buffer);
    auto* vkHeap = checked_cast<Heap*>(heap);
    
    if(!vkHeap || vkHeap->memory == VK_NULL_HANDLE)
        return false;
    
    // Binding to a heap means the heap owns the memory, not the buffer
    buffer->memory = VK_NULL_HANDLE;
    
    VkResult res = vkBindBufferMemory(m_context.device, buffer->buffer, vkHeap->memory, offset);
    return res == VK_SUCCESS;
}

BufferHandle Device::createHandleForNativeBuffer(ObjectType objectType, Object _buffer, const BufferDesc& desc){
    if(objectType != ObjectTypes::VK_Buffer)
        return nullptr;
    
    auto* nativeBuffer = static_cast<VkBuffer_T*>(_buffer);
    if(nativeBuffer == VK_NULL_HANDLE)
        return nullptr;
    
    auto* buffer = new Buffer(m_context, m_allocator);
    buffer->desc = desc;
    buffer->buffer = nativeBuffer;
    buffer->managed = false;
    
    if(m_context.extensions.buffer_device_address){
        VkBufferDeviceAddressInfo addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = nativeBuffer;
        buffer->deviceAddress = vkGetBufferDeviceAddress(m_context.device, &addressInfo);
    }
    
    return RefCountPtr<IBuffer, BlankDeleter<IBuffer>>(buffer, AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::writeBuffer(IBuffer* _buffer, const void* data, usize dataSize, u64 destOffsetBytes){
    auto* buffer = checked_cast<Buffer*>(_buffer);
    
    UploadManager* uploadMgr = m_device.getUploadManager();
    Buffer* stagingBuffer = nullptr;
    u64 stagingOffset = 0;
    void* cpuVA = nullptr;
    
    if(!uploadMgr->suballocateBuffer(dataSize, &stagingBuffer, &stagingOffset, &cpuVA, 0)){
        NWB_ASSERT_MSG(false, NWB_TEXT("Failed to suballocate staging buffer for writeBuffer"));
        return;
    }
    
    NWB_MEMCPY(cpuVA, dataSize, data, dataSize);
    
    VkBufferCopy region{};
    region.srcOffset = stagingOffset;
    region.dstOffset = destOffsetBytes;
    region.size = dataSize;
    
    vkCmdCopyBuffer(currentCmdBuf->cmdBuf, stagingBuffer->buffer, buffer->buffer, 1, &region);
    
    currentCmdBuf->referencedResources.push_back(_buffer);
    currentCmdBuf->referencedResources.push_back(stagingBuffer);
}

void CommandList::clearBufferUInt(IBuffer* _buffer, u32 clearValue){
    auto* buffer = checked_cast<Buffer*>(_buffer);
    
    vkCmdFillBuffer(currentCmdBuf->cmdBuf, buffer->buffer, 0, VK_WHOLE_SIZE, clearValue);
    currentCmdBuf->referencedResources.push_back(_buffer);
}

void CommandList::copyBuffer(IBuffer* _dest, u64 destOffsetBytes, IBuffer* _src, u64 srcOffsetBytes, u64 dataSizeBytes){
    auto* dest = checked_cast<Buffer*>(_dest);
    auto* src = checked_cast<Buffer*>(_src);
    
    VkBufferCopy region{};
    region.srcOffset = srcOffsetBytes;
    region.dstOffset = destOffsetBytes;
    region.size = dataSizeBytes;
    
    vkCmdCopyBuffer(currentCmdBuf->cmdBuf, src->buffer, dest->buffer, 1, &region);
    
    currentCmdBuf->referencedResources.push_back(_src);
    currentCmdBuf->referencedResources.push_back(_dest);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

