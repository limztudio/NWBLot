// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Buffer::Buffer(const VulkanContext& context, VulkanAllocator& allocator)
    : RefCounter<IBuffer>(*context.threadPool)
    , m_context(context)
    , m_allocator(allocator)
    , m_versionTracking(Alloc::CustomAllocator<u64>(*context.objectArena))
{}
Buffer::~Buffer(){
    if(m_managed){
        if(m_buffer != VK_NULL_HANDLE){
            vkDestroyBuffer(m_context.device, m_buffer, m_context.allocationCallbacks);
            m_buffer = VK_NULL_HANDLE;
        }

        m_allocator.freeBufferMemory(this);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


BufferHandle Device::createBuffer(const BufferDesc& d){
    VkResult res = VK_SUCCESS;

    auto* buffer = NewArenaObject<Buffer>(*m_context.objectArena, m_context, m_allocator);
    buffer->m_desc = d;

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

        buffer->m_versionTracking.resize(d.maxVersions);
        std::fill(buffer->m_versionTracking.begin(), buffer->m_versionTracking.end(), 0);
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usageFlags;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    res = vkCreateBuffer(m_context.device, &bufferInfo, m_context.allocationCallbacks, &buffer->m_buffer);
    NWB_ASSERT(res == VK_SUCCESS);

    if(!d.isVirtual){
        res = m_allocator.allocateBufferMemory(buffer, m_context.extensions.buffer_device_address && (usageFlags & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT));
        NWB_ASSERT(res == VK_SUCCESS);

        if(d.isVolatile || d.cpuAccess != CpuAccessMode::None){
            res = vkMapMemory(m_context.device, buffer->m_memory, 0, size, 0, &buffer->m_mappedMemory);
            NWB_ASSERT(res == VK_SUCCESS);
        }

        if(m_context.extensions.buffer_device_address){
            VkBufferDeviceAddressInfo addressInfo{};
            addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            addressInfo.buffer = buffer->m_buffer;
            buffer->m_deviceAddress = vkGetBufferDeviceAddress(m_context.device, &addressInfo);
        }
    }

    return BufferHandle(buffer, BufferHandle::deleter_type(m_context.objectArena), AdoptRef);
}

void* Device::mapBuffer(IBuffer* _buffer, CpuAccessMode::Enum){
    VkResult res = VK_SUCCESS;

    auto* buffer = static_cast<Buffer*>(_buffer);

    if(buffer->m_mappedMemory)
        return buffer->m_mappedMemory;

    void* data = nullptr;
    res = vkMapMemory(m_context.device, buffer->m_memory, 0, VK_WHOLE_SIZE, 0, &data);
    NWB_ASSERT(res == VK_SUCCESS);

    buffer->m_mappedMemory = data;
    return data;
}

void Device::unmapBuffer(IBuffer* _buffer){
    auto* buffer = static_cast<Buffer*>(_buffer);

    if(buffer->m_mappedMemory && !buffer->m_desc.isVolatile && buffer->m_desc.cpuAccess == CpuAccessMode::None){
        vkUnmapMemory(m_context.device, buffer->m_memory);
        buffer->m_mappedMemory = nullptr;
    }
}

MemoryRequirements Device::getBufferMemoryRequirements(IBuffer* _buffer){
    auto* buffer = static_cast<Buffer*>(_buffer);

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_context.device, buffer->m_buffer, &memRequirements);

    MemoryRequirements result;
    result.size = memRequirements.size;
    result.alignment = memRequirements.alignment;
    return result;
}

bool Device::bindBufferMemory(IBuffer* _buffer, IHeap* heap, u64 offset){
    VkResult res = VK_SUCCESS;

    auto* buffer = static_cast<Buffer*>(_buffer);
    auto* vkHeap = checked_cast<Heap*>(heap);

    if(!vkHeap || vkHeap->m_memory == VK_NULL_HANDLE)
        return false;

    // Binding to a heap means the heap owns the memory, not the buffer
    buffer->m_memory = VK_NULL_HANDLE;

    res = vkBindBufferMemory(m_context.device, buffer->m_buffer, vkHeap->m_memory, offset);
    return res == VK_SUCCESS;
}

BufferHandle Device::createHandleForNativeBuffer(ObjectType objectType, Object _buffer, const BufferDesc& desc){
    if(objectType != ObjectTypes::VK_Buffer)
        return nullptr;

    auto* nativeBuffer = static_cast<VkBuffer_T*>(_buffer);
    if(nativeBuffer == VK_NULL_HANDLE)
        return nullptr;

    auto* buffer = NewArenaObject<Buffer>(*m_context.objectArena, m_context, m_allocator);
    buffer->m_desc = desc;
    buffer->m_buffer = nativeBuffer;
    buffer->m_managed = false;

    if(m_context.extensions.buffer_device_address){
        VkBufferDeviceAddressInfo addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = nativeBuffer;
        buffer->m_deviceAddress = vkGetBufferDeviceAddress(m_context.device, &addressInfo);
    }

    return BufferHandle(buffer, BufferHandle::deleter_type(m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::writeBuffer(IBuffer* _buffer, const void* data, usize dataSize, u64 destOffsetBytes){
    auto* buffer = checked_cast<Buffer*>(_buffer);

    UploadManager* uploadMgr = m_device.getUploadManager();
    Buffer* stagingBuffer = nullptr;
    u64 stagingOffset = 0;
    void* cpuVA = nullptr;

    if(!uploadMgr->suballocateBuffer(dataSize, &stagingBuffer, &stagingOffset, &cpuVA, 0)){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to suballocate staging buffer for writeBuffer"));
        return;
    }

    __hidden_vulkan::CopyHostMemory(taskPool(), cpuVA, data, dataSize);

    VkBufferCopy region{};
    region.srcOffset = stagingOffset;
    region.dstOffset = destOffsetBytes;
    region.size = dataSize;

    vkCmdCopyBuffer(m_currentCmdBuf->m_cmdBuf, stagingBuffer->m_buffer, buffer->m_buffer, 1, &region);

    m_currentCmdBuf->m_referencedResources.push_back(_buffer);
    m_currentCmdBuf->m_referencedResources.push_back(stagingBuffer);
}

void CommandList::clearBufferUInt(IBuffer* _buffer, u32 clearValue){
    auto* buffer = checked_cast<Buffer*>(_buffer);

    vkCmdFillBuffer(m_currentCmdBuf->m_cmdBuf, buffer->m_buffer, 0, VK_WHOLE_SIZE, clearValue);
    m_currentCmdBuf->m_referencedResources.push_back(_buffer);
}

void CommandList::copyBuffer(IBuffer* _dest, u64 destOffsetBytes, IBuffer* _src, u64 srcOffsetBytes, u64 dataSizeBytes){
    auto* dest = checked_cast<Buffer*>(_dest);
    auto* src = checked_cast<Buffer*>(_src);

    VkBufferCopy region{};
    region.srcOffset = srcOffsetBytes;
    region.dstOffset = destOffsetBytes;
    region.size = dataSizeBytes;

    vkCmdCopyBuffer(m_currentCmdBuf->m_cmdBuf, src->m_buffer, dest->m_buffer, 1, &region);

    m_currentCmdBuf->m_referencedResources.push_back(_src);
    m_currentCmdBuf->m_referencedResources.push_back(_dest);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

