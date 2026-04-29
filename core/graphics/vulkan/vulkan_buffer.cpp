// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsBufferRangeInBounds(const BufferDesc& desc, u64 offsetBytes, u64 sizeBytes){
    return offsetBytes <= desc.byteSize && sizeBytes <= desc.byteSize - offsetBytes;
}

bool BufferRangesOverlap(u64 firstOffsetBytes, u64 firstSizeBytes, u64 secondOffsetBytes, u64 secondSizeBytes){
    if(firstSizeBytes == 0 || secondSizeBytes == 0)
        return false;
    if(firstSizeBytes > Limit<u64>::s_Max - firstOffsetBytes || secondSizeBytes > Limit<u64>::s_Max - secondOffsetBytes)
        return true;

    const u64 firstEnd = firstOffsetBytes + firstSizeBytes;
    const u64 secondEnd = secondOffsetBytes + secondSizeBytes;
    return firstOffsetBytes < secondEnd && secondOffsetBytes < firstEnd;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Buffer::Buffer(const VulkanContext& context, VulkanAllocator& allocator)
    : RefCounter<IBuffer>(context.threadPool)
    , m_versionTracking(Alloc::CustomAllocator<u64>(context.objectArena))
    , m_bufferViews(Alloc::CustomAllocator<BufferViewEntry>(context.objectArena))
    , m_context(context)
    , m_allocator(allocator)
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
    const FormatInfo& formatInfo = GetFormatInfo(format);
    if(formatInfo.bytesPerBlock == 0)
        return VK_NULL_HANDLE;

    if(byteOffset >= m_desc.byteSize)
        return VK_NULL_HANDLE;

    const u64 maxRange = m_desc.byteSize - byteOffset;
    const u64 offsetAlignment = Max<u64>(m_context.physicalDeviceProperties.limits.minTexelBufferOffsetAlignment, 1u);
    if((byteOffset % offsetAlignment) != 0)
        return VK_NULL_HANDLE;
    if((byteOffset % formatInfo.bytesPerBlock) != 0)
        return VK_NULL_HANDLE;

    u64 resolvedSize = byteSize;
    if(resolvedSize == 0 || resolvedSize == VK_WHOLE_SIZE)
        resolvedSize = maxRange;
    else if(resolvedSize > maxRange)
        resolvedSize = maxRange;
    if(resolvedSize == 0)
        return VK_NULL_HANDLE;
    if((resolvedSize % formatInfo.bytesPerBlock) != 0)
        return VK_NULL_HANDLE;
    if((resolvedSize / formatInfo.bytesPerBlock) > m_context.physicalDeviceProperties.limits.maxTexelBufferElements)
        return VK_NULL_HANDLE;

    ScopedLock lock(m_bufferViewsMutex);
    for(const auto& viewEntry : m_bufferViews){
        if(viewEntry.format == format && viewEntry.byteOffset == byteOffset && viewEntry.byteSize == resolvedSize)
            return viewEntry.view;
    }

    auto viewInfo = VulkanDetail::MakeVkStruct<VkBufferViewCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO);
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

    if(d.byteSize == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer: byte size is zero"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create buffer: byte size is zero"));
        return nullptr;
    }
    if(d.isVolatile && d.maxVersions == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create volatile buffer: maxVersions is zero"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create volatile buffer: maxVersions is zero"));
        return nullptr;
    }

    auto* buffer = NewArenaObject<Buffer>(m_context.objectArena, m_context, m_allocator);
    buffer->m_desc = d;

    VkBufferUsageFlags usageFlags =
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT
        | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT
    ;

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
    if(m_context.extensions.EXT_opacity_micromap){
        if(d.isAccelStructBuildInput)
            usageFlags |= VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT;
        if(d.isAccelStructStorage)
            usageFlags |= VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT;
    }
    if(d.isShaderBindingTable){
        if(!m_context.extensions.KHR_ray_tracing_pipeline || !m_context.extensions.buffer_device_address){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create shader binding table buffer: ray tracing pipeline and buffer device address support are required"));
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create shader binding table buffer: ray tracing pipeline and buffer device address support are required"));
            DestroyArenaObject(m_context.objectArena, buffer);
            return nullptr;
        }

        usageFlags |= VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    }
    if(m_context.extensions.buffer_device_address)
        usageFlags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    u64 size = d.byteSize;

    if(d.isVolatile){
        const u64 alignment = Max<u64>(m_context.physicalDeviceProperties.limits.minUniformBufferOffsetAlignment, 1u);
        u64 alignedSize = 0;
        if(!AlignUpU64Checked(size, alignment, alignedSize)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create volatile buffer: aligned size overflows"));
            DestroyArenaObject(m_context.objectArena, buffer);
            return nullptr;
        }
        if(alignedSize > static_cast<u64>(-1) / d.maxVersions){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create volatile buffer: versioned size overflows"));
            DestroyArenaObject(m_context.objectArena, buffer);
            return nullptr;
        }
        size = alignedSize * d.maxVersions;

        buffer->m_versionTracking.resize(d.maxVersions);
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

void* Device::mapBuffer(IBuffer* bufferResource, CpuAccessMode::Enum){
    VkResult res = VK_SUCCESS;

    if(!bufferResource){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map buffer: buffer is null"));
        return nullptr;
    }

    auto* buffer = static_cast<Buffer*>(bufferResource);
    if(buffer->m_memory == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map buffer: buffer has no bound memory"));
        return nullptr;
    }

    auto invalidateReadRange = [&]() -> bool{
        if(buffer->m_desc.cpuAccess != CpuAccessMode::Read)
            return true;

        auto range = VulkanDetail::MakeVkStruct<VkMappedMemoryRange>(VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE);
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
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map buffer memory: {}"), ResultToString(res));
        return nullptr;
    }

    buffer->m_mappedMemory = data;
    if(!invalidateReadRange()){
        vkUnmapMemory(m_context.device, buffer->m_memory);
        buffer->m_mappedMemory = nullptr;
        return nullptr;
    }
    return data;
}

void Device::unmapBuffer(IBuffer* bufferResource){
    if(!bufferResource)
        return;

    auto* buffer = static_cast<Buffer*>(bufferResource);

    if(buffer->m_mappedMemory && !buffer->m_desc.isVolatile && buffer->m_desc.cpuAccess == CpuAccessMode::None){
        vkUnmapMemory(m_context.device, buffer->m_memory);
        buffer->m_mappedMemory = nullptr;
    }
}

MemoryRequirements Device::getBufferMemoryRequirements(IBuffer* bufferResource){
    if(!bufferResource){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to get buffer memory requirements: buffer is null"));
        return {};
    }

    auto* buffer = static_cast<Buffer*>(bufferResource);

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_context.device, buffer->m_buffer, &memRequirements);

    MemoryRequirements result;
    result.size = memRequirements.size;
    result.alignment = memRequirements.alignment;
    return result;
}

bool Device::validateHeapMemoryBinding(
    IHeap* heap,
    const VkMemoryRequirements& memoryRequirements,
    const u64 offset,
    const tchar* operationName,
    const tchar* resourceName,
    Heap*& outHeap
)const{
    outHeap = checked_cast<Heap*>(heap);

    if(!outHeap || outHeap->m_memory == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: heap is invalid"), operationName);
        return false;
    }
    if(outHeap->m_memoryTypeIndex >= 32u || (memoryRequirements.memoryTypeBits & (1u << outHeap->m_memoryTypeIndex)) == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: heap memory type is incompatible with the {}"), operationName, resourceName);
        return false;
    }
    const u64 alignment = Max<u64>(static_cast<u64>(memoryRequirements.alignment), 1u);
    if((offset % alignment) != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: offset {} is not aligned to required alignment {}")
            , operationName
            , offset
            , alignment
        );
        return false;
    }
    if(offset > outHeap->m_desc.capacity || static_cast<u64>(memoryRequirements.size) > outHeap->m_desc.capacity - offset){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: offset {} size {} exceeds heap capacity {}")
            , operationName
            , offset
            , static_cast<u64>(memoryRequirements.size)
            , outHeap->m_desc.capacity
        );
        return false;
    }

    return true;
}

bool Device::bindBufferMemory(IBuffer* bufferResource, IHeap* heap, u64 offset){
    VkResult res = VK_SUCCESS;

    if(!bufferResource){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind buffer memory: buffer is null"));
        return false;
    }

    auto* buffer = static_cast<Buffer*>(bufferResource);
    if(!buffer->m_desc.isVirtual){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind buffer memory: buffer was not created as virtual"));
        return false;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_context.device, buffer->m_buffer, &memRequirements);
    Heap* vkHeap = nullptr;
    if(!validateHeapMemoryBinding(heap, memRequirements, offset, NWB_TEXT("bind buffer memory"), NWB_TEXT("buffer"), vkHeap))
        return false;

    // Binding to a heap means the heap owns the memory, not the buffer
    buffer->m_memory = VK_NULL_HANDLE;

    res = vkBindBufferMemory(m_context.device, buffer->m_buffer, vkHeap->m_memory, offset);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to bind buffer memory: {}"), ResultToString(res));
        return false;
    }
    if(m_context.extensions.buffer_device_address){
        VkBufferDeviceAddressInfo addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = buffer->m_buffer;
        buffer->m_deviceAddress = vkGetBufferDeviceAddress(m_context.device, &addressInfo);
    }

    return true;
}

BufferHandle Device::createHandleForNativeBuffer(ObjectType objectType, Object nativeBufferHandle, const BufferDesc& desc){
    if(objectType != ObjectTypes::VK_Buffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: object type is not VK_Buffer"));
        return nullptr;
    }

    auto* nativeBuffer = static_cast<VkBuffer_T*>(nativeBufferHandle);
    if(nativeBuffer == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: buffer handle is null"));
        return nullptr;
    }
    if(desc.byteSize == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: byte size is zero"));
        return nullptr;
    }

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


bool CommandList::prepareUploadStaging(const void* data, const usize dataSize, const tchar* operationName, Buffer*& outStagingBuffer, u64& outStagingOffset){
    outStagingBuffer = nullptr;
    outStagingOffset = 0;

    UploadManager* uploadMgr = m_device.m_uploadManager.get();
    void* cpuVA = nullptr;

    const u64 completedUploadVersion = m_device.queueGetCompletedInstance(m_desc.queueType);
    if(!uploadMgr->suballocateBuffer(static_cast<u64>(dataSize), &outStagingBuffer, &outStagingOffset, &cpuVA, m_currentCmdBuf.get(), m_desc.queueType, completedUploadVersion)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to suballocate staging buffer for {}"), operationName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to suballocate staging buffer"));
        return false;
    }

    VulkanDetail::CopyHostMemory(taskPool(), cpuVA, data, dataSize);
    return true;
}

void CommandList::writeBuffer(IBuffer* bufferResource, const void* data, usize dataSize, u64 destOffsetBytes){
    if(!bufferResource || !data || dataSize == 0)
        return;

    auto* buffer = checked_cast<Buffer*>(bufferResource);
    const BufferDesc& desc = buffer->getDescription();
    if(!VulkanDetail::IsBufferRangeInBounds(desc, destOffsetBytes, static_cast<u64>(dataSize))){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to write buffer: destination offset {} size {} is outside buffer size {}")
            , destOffsetBytes
            , static_cast<u64>(dataSize)
            , desc.byteSize
        );
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to write buffer: destination range is outside the buffer"));
        return;
    }

    Buffer* stagingBuffer = nullptr;
    u64 stagingOffset = 0;
    if(!prepareUploadStaging(data, dataSize, NWB_TEXT("writeBuffer"), stagingBuffer, stagingOffset))
        return;

    setBufferState(bufferResource, ResourceStates::CopyDest);

    VkBufferCopy region{};
    region.srcOffset = stagingOffset;
    region.dstOffset = destOffsetBytes;
    region.size = dataSize;

    vkCmdCopyBuffer(m_currentCmdBuf->m_cmdBuf, stagingBuffer->m_buffer, buffer->m_buffer, 1, &region);

    retainResource(bufferResource);
    retainStagingBuffer(stagingBuffer);
}

void CommandList::clearBufferUInt(IBuffer* bufferResource, u32 clearValue){
    auto* buffer = checked_cast<Buffer*>(bufferResource);
    if(!buffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear buffer: buffer is null"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear buffer: buffer is null"));
        return;
    }
    if((buffer->m_desc.byteSize & 3u) != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to clear buffer: buffer size is not 4-byte aligned"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to clear buffer: buffer size is not 4-byte aligned"));
        return;
    }

    setBufferState(bufferResource, ResourceStates::CopyDest);
    vkCmdFillBuffer(m_currentCmdBuf->m_cmdBuf, buffer->m_buffer, 0, VK_WHOLE_SIZE, clearValue);
    retainResource(bufferResource);
}

void CommandList::copyBuffer(IBuffer* destResource, u64 destOffsetBytes, IBuffer* srcResource, u64 srcOffsetBytes, u64 dataSizeBytes){
    if(!destResource || !srcResource || dataSizeBytes == 0)
        return;

    auto* dest = checked_cast<Buffer*>(destResource);
    auto* src = checked_cast<Buffer*>(srcResource);
    const BufferDesc& destDesc = dest->getDescription();
    const BufferDesc& srcDesc = src->getDescription();

    if(!VulkanDetail::IsBufferRangeInBounds(destDesc, destOffsetBytes, dataSizeBytes)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to copy buffer: destination offset {} size {} is outside buffer size {}")
            , destOffsetBytes
            , dataSizeBytes
            , destDesc.byteSize
        );
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to copy buffer: destination range is outside the buffer"));
        return;
    }

    if(!VulkanDetail::IsBufferRangeInBounds(srcDesc, srcOffsetBytes, dataSizeBytes)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to copy buffer: source offset {} size {} is outside buffer size {}")
            , srcOffsetBytes
            , dataSizeBytes
            , srcDesc.byteSize
        );
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to copy buffer: source range is outside the buffer"));
        return;
    }

    if(dest->m_buffer == src->m_buffer && VulkanDetail::BufferRangesOverlap(destOffsetBytes, dataSizeBytes, srcOffsetBytes, dataSizeBytes)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to copy buffer: source and destination ranges overlap in the same buffer"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to copy buffer: source and destination ranges overlap in the same buffer"));
        return;
    }

    setBufferState(srcResource, ResourceStates::CopySource);
    setBufferState(destResource, ResourceStates::CopyDest);

    VkBufferCopy region{};
    region.srcOffset = srcOffsetBytes;
    region.dstOffset = destOffsetBytes;
    region.size = dataSizeBytes;

    vkCmdCopyBuffer(m_currentCmdBuf->m_cmdBuf, src->m_buffer, dest->m_buffer, 1, &region);

    retainResource(srcResource);
    retainResource(destResource);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

