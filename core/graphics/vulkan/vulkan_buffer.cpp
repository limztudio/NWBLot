// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Buffer::Buffer(const VulkanContext& context, VulkanAllocator& allocator)
    : RefCounter<IBuffer>(context.threadPool)
    , m_context(context)
    , m_allocator(allocator)
    , m_versionTracking(Alloc::CustomAllocator<u64>(context.objectArena))
    , m_bufferViews(Alloc::CustomAllocator<BufferViewEntry>(context.objectArena))
{}
Buffer::~Buffer(){
    for(auto& viewEntry : m_bufferViews){
        if(viewEntry.view != VK_NULL_HANDLE){
            vkDestroyBufferView(m_context.device, viewEntry.view, m_context.allocationCallbacks);
            viewEntry.view = VK_NULL_HANDLE;
        }
    }
    m_bufferViews.clear();

    if(m_managed){
        if(m_buffer != VK_NULL_HANDLE){
            vkDestroyBuffer(m_context.device, m_buffer, m_context.allocationCallbacks);
            m_buffer = VK_NULL_HANDLE;
        }

        m_allocator.freeBufferMemory(this);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VkBufferView Buffer::getView(Format::Enum format, u64 byteOffset, u64 byteSize){
    VkResult res = VK_SUCCESS;

    if(m_buffer == VK_NULL_HANDLE || format == Format::UNKNOWN)
        return VK_NULL_HANDLE;

    const VkFormat vkFormat = ConvertFormat(format);
    if(vkFormat == VK_FORMAT_UNDEFINED)
        return VK_NULL_HANDLE;

    if(byteOffset >= m_desc.byteSize)
        return VK_NULL_HANDLE;
    const u64 maxRange = m_desc.byteSize - byteOffset;

    u64 resolvedSize = byteSize;
    if(resolvedSize == 0 || resolvedSize == VK_WHOLE_SIZE){
        resolvedSize = maxRange;
    }
    else if(resolvedSize > maxRange)
        resolvedSize = maxRange;
    if(resolvedSize == 0)
        return VK_NULL_HANDLE;

    ScopedLock lock(m_bufferViewsMutex);
    for(const auto& viewEntry : m_bufferViews){
        if(viewEntry.format == format && viewEntry.byteOffset == byteOffset && viewEntry.byteSize == resolvedSize)
            return viewEntry.view;
    }

    VkBufferViewCreateInfo viewInfo = { VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO };
    viewInfo.buffer = m_buffer;
    viewInfo.format = vkFormat;
    viewInfo.offset = byteOffset;
    viewInfo.range = resolvedSize;

    VkBufferView view = VK_NULL_HANDLE;
    res = vkCreateBufferView(m_context.device, &viewInfo, m_context.allocationCallbacks, &view);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer view: {}"), ResultToString(res));
        return VK_NULL_HANDLE;
    }

    BufferViewEntry entry;
    entry.format = format;
    entry.byteOffset = byteOffset;
    entry.byteSize = resolvedSize;
    entry.view = view;
    m_bufferViews.push_back(entry);

    return view;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


BufferHandle Device::createBuffer(const BufferDesc& d){
    VkResult res = VK_SUCCESS;

    auto* buffer = NewArenaObject<Buffer>(m_context.objectArena, m_context, m_allocator);
    buffer->m_desc = d;

    VkBufferUsageFlags usageFlags =
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
        VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;

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
    if(res != VK_SUCCESS){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create buffer"));
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, buffer);
        return nullptr;
    }

    if(!d.isVirtual){
        res = m_allocator.allocateBufferMemory(buffer, m_context.extensions.buffer_device_address && (usageFlags & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT));
        if(res != VK_SUCCESS){
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to allocate buffer memory"));
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate buffer memory: {}"), ResultToString(res));
            DestroyArenaObject(m_context.objectArena, buffer);
            return nullptr;
        }

        if(d.isVolatile || d.cpuAccess != CpuAccessMode::None){
            res = vkMapMemory(m_context.device, buffer->m_memory, 0, size, 0, &buffer->m_mappedMemory);
            if(res != VK_SUCCESS){
                NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to map buffer memory"));
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map buffer memory: {}"), ResultToString(res));
                DestroyArenaObject(m_context.objectArena, buffer);
                return nullptr;
            }
        }

        if(m_context.extensions.buffer_device_address){
            VkBufferDeviceAddressInfo addressInfo{};
            addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            addressInfo.buffer = buffer->m_buffer;
            buffer->m_deviceAddress = vkGetBufferDeviceAddress(m_context.device, &addressInfo);
        }
    }

    return BufferHandle(buffer, BufferHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

void* Device::mapBuffer(IBuffer* _buffer, CpuAccessMode::Enum){
    VkResult res = VK_SUCCESS;

    if(!_buffer)
        return nullptr;

    auto* buffer = static_cast<Buffer*>(_buffer);
    if(buffer->m_memory == VK_NULL_HANDLE)
        return nullptr;

    auto invalidateReadRange = [&]() -> bool{
        if(buffer->m_desc.cpuAccess != CpuAccessMode::Read)
            return true;

        VkMappedMemoryRange range = { VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
        range.memory = buffer->m_memory;
        range.offset = 0;
        range.size = VK_WHOLE_SIZE;
        res = vkInvalidateMappedMemoryRanges(m_context.device, 1, &range);
        if(res != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to invalidate readback buffer mapping: {}"), ResultToString(res));
            return false;
        }
        return true;
    };

    if(buffer->m_mappedMemory){
        if(!invalidateReadRange())
            return nullptr;
        return buffer->m_mappedMemory;
    }

    void* data = nullptr;
    res = vkMapMemory(m_context.device, buffer->m_memory, 0, VK_WHOLE_SIZE, 0, &data);
    if(res != VK_SUCCESS)
        return nullptr;

    buffer->m_mappedMemory = data;
    if(!invalidateReadRange())
        return nullptr;
    return data;
}

void Device::unmapBuffer(IBuffer* _buffer){
    if(!_buffer)
        return;

    auto* buffer = static_cast<Buffer*>(_buffer);

    if(buffer->m_mappedMemory && !buffer->m_desc.isVolatile && buffer->m_desc.cpuAccess == CpuAccessMode::None){
        vkUnmapMemory(m_context.device, buffer->m_memory);
        buffer->m_mappedMemory = nullptr;
    }
}

MemoryRequirements Device::getBufferMemoryRequirements(IBuffer* _buffer){
    if(!_buffer)
        return {};

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

    if(!_buffer)
        return false;

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

    auto* buffer = NewArenaObject<Buffer>(m_context.objectArena, m_context, m_allocator);
    buffer->m_desc = desc;
    buffer->m_buffer = nativeBuffer;
    buffer->m_managed = false;

    if(m_context.extensions.buffer_device_address){
        VkBufferDeviceAddressInfo addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = nativeBuffer;
        buffer->m_deviceAddress = vkGetBufferDeviceAddress(m_context.device, &addressInfo);
    }

    return BufferHandle(buffer, BufferHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::writeBuffer(IBuffer* _buffer, const void* data, usize dataSize, u64 destOffsetBytes){
    if(!_buffer || !data || dataSize == 0)
        return;

    auto* buffer = checked_cast<Buffer*>(_buffer);

    UploadManager* uploadMgr = m_device.m_uploadManager.get();
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

