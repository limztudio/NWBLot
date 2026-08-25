// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"

#include <core/common/log.h>


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

[[nodiscard]] static VkBufferUsageFlags PickBufferUsage(const VulkanContext& context, const BufferDesc& desc){
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    usage |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
    if(desc.isVertexBuffer)
        usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if(desc.isIndexBuffer)
        usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if(desc.isConstantBuffer)
        usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if(desc.structStride != 0u || desc.canHaveUAVs || desc.canHaveRawViews)
        usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if(desc.isDrawIndirectArgs)
        usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if(desc.isAccelStructBuildInput)
        usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    if(desc.isAccelStructStorage)
        usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
    if(context.extensions.EXT_opacity_micromap){
        if(desc.isAccelStructBuildInput)
            usage |= VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT;
        if(desc.isAccelStructStorage)
            usage |= VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT;
    }
    if(desc.isShaderBindingTable)
        usage |= VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if(context.extensions.buffer_device_address)
        usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    return usage;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Buffer::Buffer(const VulkanContext& context, VulkanAllocator& allocator)
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_versionTracking(context.objectArena)
    , m_bufferViews(context.objectArena)
    , m_context(context)
    , m_allocator(allocator)
{}
Buffer::~Buffer(){
    const VkBuffer registeredNativeBuffer = m_buffer;

    for(auto& viewEntry : m_bufferViews){
        if(viewEntry.view != VK_NULL_HANDLE){
            vkDestroyBufferView(m_context.device, viewEntry.view, m_context.allocationCallbacks);
            viewEntry.view = VK_NULL_HANDLE;
        }
    }
    m_bufferViews.clear();

    {
        ScopedLock bindingLock(m_memoryBindingMutex);
        if(m_boundHeap){
            Heap* const boundHeap = m_boundHeap.get();
            {
                ScopedLock heapLock(boundHeap->m_bindingMutex);
                if(m_buffer != VK_NULL_HANDLE){
                    vkDestroyBuffer(m_context.device, m_buffer, m_context.allocationCallbacks);
                    m_buffer = VK_NULL_HANDLE;
                }
                boundHeap->eraseBindingReservationLocked(this);
            }
            m_heapBindingRange = {};
            m_mappedMemory = nullptr;
            m_persistentlyMapped = false;
            m_requiresInvalidate = false;
            m_boundHeap.reset();
        }
        else if(m_managed){
            if(m_desc.isVirtual){
                if(m_buffer != VK_NULL_HANDLE){
                    vkDestroyBuffer(m_context.device, m_buffer, m_context.allocationCallbacks);
                    m_buffer = VK_NULL_HANDLE;
                }
            }
            else
                m_allocator.destroyBuffer(*this);
        }
    }

    m_allocator.unregisterBufferNativeIdentity(registeredNativeBuffer, *this);
}

Object Buffer::getNativeHandle(ObjectType objectType){
    if(objectType != ObjectTypes::VK_Buffer)
        return nullptr;

    return Object(m_buffer);
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
    CpuAccessMode::Enum effectiveCpuAccess = CpuAccessMode::None;
    if(!VulkanDetail::TryResolveBufferCpuAccess(d.cpuAccess, d.isVolatile, effectiveCpuAccess)){
        if(d.isVolatile && d.cpuAccess == CpuAccessMode::Read){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer: a volatile buffer cannot request CPU read access"));
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Volatile buffer CPU access contradicts its write-only contract"));
        }
        else{
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer: invalid CPU access mode"));
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create buffer: invalid CPU access mode"));
        }
        return nullptr;
    }

    auto* buffer = NewArenaObject<Buffer>(m_context.objectArena, m_context, m_allocator);
    buffer->m_desc = d;
    buffer->m_creationDesc = d;

    const VkBufferUsageFlags usageFlags = VulkanDetail::PickBufferUsage(m_context, d);
    if(d.isShaderBindingTable){
        if(!m_context.extensions.KHR_ray_tracing_pipeline || !m_context.extensions.buffer_device_address){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create shader binding table buffer: ray tracing pipeline and buffer device address support are required"));
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create shader binding table buffer: ray tracing pipeline and buffer device address support are required"));
            DestroyArenaObject(m_context.objectArena, buffer);
            return nullptr;
        }

    }

    u64 size = d.byteSize;

    if(d.isVolatile){
        const u64 alignment = Max<u64>(m_context.physicalDeviceProperties.limits.minUniformBufferOffsetAlignment, 1u);
        u64 alignedSize = 0;
        if(!AlignUpU64Checked(size, alignment, alignedSize)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create volatile buffer: aligned size overflows"));
            DestroyArenaObject(m_context.objectArena, buffer);
            return nullptr;
        }
        if(alignedSize > Limit<u64>::s_Max / d.maxVersions){
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
    buffer->m_usage = usageFlags;
    const QueueFamilySharingInfo sharingInfo = ResolveQueueFamilySharing(d.queueSharing, m_context);
    bufferInfo.sharingMode = sharingInfo.mode;
    bufferInfo.queueFamilyIndexCount = sharingInfo.familyIndexCount;
    bufferInfo.pQueueFamilyIndices = sharingInfo.data();

    if(d.isVirtual)
        res = vkCreateBuffer(m_context.device, &bufferInfo, m_context.allocationCallbacks, &buffer->m_buffer);
    else
        res = m_allocator.createBuffer(*buffer, bufferInfo);
    if(res != VK_SUCCESS){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create buffer"));
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, buffer);
        return nullptr;
    }
    if(!m_allocator.tryRegisterBufferNativeIdentity(*buffer)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer: native buffer identity is already registered"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: A newly created buffer duplicated a live native buffer identity"));
        DestroyArenaObject(m_context.objectArena, buffer);
        return nullptr;
    }

    if(!d.isVirtual){
        if(m_context.extensions.buffer_device_address){
            VkBufferDeviceAddressInfo addressInfo{};
            addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            addressInfo.buffer = buffer->m_buffer;
            buffer->m_deviceAddress = vkGetBufferDeviceAddress(m_context.device, &addressInfo);
        }
    }

    return BufferHandle(buffer, BufferHandle::deleter_type(&m_context.objectArena), AdoptRef);
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
    buffer->m_creationDesc = desc;
    buffer->m_buffer = nativeBuffer;
    buffer->m_usage = VulkanDetail::PickBufferUsage(m_context, desc);
    buffer->m_managed = false;

    if(!m_allocator.tryRegisterBufferNativeIdentity(*buffer)){
        NWB_LOGGER_WARNING(
            NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: a live wrapper already exists")
        );
        DestroyArenaObject(m_context.objectArena, buffer);
        return nullptr;
    }

    if(m_context.extensions.buffer_device_address){
        VkBufferDeviceAddressInfo addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = nativeBuffer;
        buffer->m_deviceAddress = vkGetBufferDeviceAddress(m_context.device, &addressInfo);
    }

    return BufferHandle(buffer, BufferHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CommandList::prepareUploadStaging(
    const usize dataSize,
    const tchar* operationName,
    Buffer*& outStagingBuffer,
    u64& outStagingOffset,
    void*& outCpuVA,
    const u32 alignment
){
    outStagingBuffer = nullptr;
    outStagingOffset = 0;
    outCpuVA = nullptr;

    UploadManager& uploadMgr = m_device.m_uploadManager;

    const u64 completedUploadVersion = m_device.queueGetCompletedInstance(m_desc.physicalQueue);
    if(!uploadMgr.suballocateBuffer(
        static_cast<u64>(dataSize),
        &outStagingBuffer,
        &outStagingOffset,
        &outCpuVA,
        m_currentCmdBuf.get(),
        m_desc.physicalQueue,
        completedUploadVersion,
        alignment
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to suballocate staging buffer for {}"), operationName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to suballocate staging buffer"));
        return false;
    }

    return true;
}

bool CommandList::prepareUploadStaging(
    const void* data,
    const usize dataSize,
    const tchar* operationName,
    Buffer*& outStagingBuffer,
    u64& outStagingOffset,
    const u32 alignment
){
    void* cpuVA = nullptr;
    if(!prepareUploadStaging(dataSize, operationName, outStagingBuffer, outStagingOffset, cpuVA, alignment))
        return false;

    VulkanDetail::CopyHostMemory(taskPool(), cpuVA, data, dataSize);
    return true;
}

bool CommandList::tryWriteBuffer(Buffer* bufferResource, const void* data, usize dataSize, u64 destOffsetBytes){
    if(dataSize == 0)
        return false;

    if(!recordAndValidateCommandCapability(GpuQueueCapability::Transfer, NWB_TEXT("write buffer")))
        return false;
    if(!VulkanDetail::AreAllPointersValid(bufferResource, data)){
        rejectCommandRecording(NWB_TEXT("write buffer"), NWB_TEXT("buffer or data is null"));
        return false;
    }

    Buffer& buffer = *bufferResource;
    const BufferDesc& desc = buffer.getDescription();
    if(!VulkanDetail::IsBufferRangeInBounds(desc, destOffsetBytes, static_cast<u64>(dataSize))){
        rejectCommandRecording(NWB_TEXT("write buffer"), NWB_TEXT("destination range is outside the buffer"));
        return false;
    }
    if((destOffsetBytes & s_BufferAlignmentMask) != 0u || (dataSize & s_BufferAlignmentMask) != 0u){
        rejectCommandRecording(NWB_TEXT("write buffer"), NWB_TEXT("copy offset and size must be 4-byte aligned"));
        return false;
    }
    if(!validateBufferForGpuState(bufferResource, ResourceStates::CopyDest, NWB_TEXT("write buffer")))
        return false;

    Buffer* stagingBuffer = nullptr;
    u64 stagingOffset = 0;
    if(!prepareUploadStaging(data, dataSize, NWB_TEXT("writeBuffer"), stagingBuffer, stagingOffset)){
        rejectCommandRecording(NWB_TEXT("write buffer"), NWB_TEXT("staging allocation failed"));
        return false;
    }

    endActiveRenderPass();
    setBufferState(bufferResource, ResourceStates::CopyDest);
    if(m_commandRecordingFailed)
        return false;

    VkBufferCopy region{};
    region.srcOffset = stagingOffset;
    region.dstOffset = destOffsetBytes;
    region.size = dataSize;

    vkCmdCopyBuffer(m_currentCmdBuf->m_cmdBuf, stagingBuffer->m_buffer, buffer.m_buffer, 1, &region);

    retainResource(bufferResource);
    retainStagingBuffer(*stagingBuffer);
    return true;
}

void CommandList::writeBuffer(Buffer* bufferResource, const void* data, usize dataSize, u64 destOffsetBytes){
    if(!tryWriteBuffer(bufferResource, data, dataSize, destOffsetBytes))
        return;
}

void CommandList::clearBufferUInt(Buffer* bufferResource, u32 clearValue){
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Transfer, NWB_TEXT("clear buffer")))
        return;
    if(!VulkanDetail::AreAllPointersValid(bufferResource)){
        rejectCommandRecording(NWB_TEXT("clear buffer"), NWB_TEXT("buffer is null"));
        return;
    }

    Buffer& buffer = *bufferResource;
    if((buffer.m_desc.byteSize & s_BufferAlignmentMask) != 0u){
        rejectCommandRecording(NWB_TEXT("clear buffer"), NWB_TEXT("buffer size is not 4-byte aligned"));
        return;
    }
    if(!validateBufferForGpuState(bufferResource, ResourceStates::CopyDest, NWB_TEXT("clear buffer")))
        return;

    endActiveRenderPass();
    setBufferState(bufferResource, ResourceStates::CopyDest);
    if(m_commandRecordingFailed)
        return;

    vkCmdFillBuffer(m_currentCmdBuf->m_cmdBuf, buffer.m_buffer, 0, VK_WHOLE_SIZE, clearValue);
    retainResource(bufferResource);
}

void CommandList::copyBuffer(Buffer* destResource, u64 destOffsetBytes, Buffer* srcResource, u64 srcOffsetBytes, u64 dataSizeBytes){
    if(dataSizeBytes == 0)
        return;

    if(!recordAndValidateCommandCapability(GpuQueueCapability::Transfer, NWB_TEXT("copy buffer")))
        return;
    if(!VulkanDetail::AreAllPointersValid(destResource, srcResource)){
        rejectCommandRecording(NWB_TEXT("copy buffer"), NWB_TEXT("source or destination buffer is null"));
        return;
    }

    Buffer& dest = *destResource;
    Buffer& src = *srcResource;

    const BufferDesc& destDesc = dest.getDescription();
    const BufferDesc& srcDesc = src.getDescription();

    if(!VulkanDetail::IsBufferRangeInBounds(destDesc, destOffsetBytes, dataSizeBytes)){
        rejectCommandRecording(NWB_TEXT("copy buffer"), NWB_TEXT("destination range is outside the buffer"));
        return;
    }

    if(!VulkanDetail::IsBufferRangeInBounds(srcDesc, srcOffsetBytes, dataSizeBytes)){
        rejectCommandRecording(NWB_TEXT("copy buffer"), NWB_TEXT("source range is outside the buffer"));
        return;
    }

    if(dest.m_buffer == src.m_buffer && VulkanDetail::BufferRangesOverlap(destOffsetBytes, dataSizeBytes, srcOffsetBytes, dataSizeBytes)){
        rejectCommandRecording(NWB_TEXT("copy buffer"), NWB_TEXT("source and destination ranges overlap in the same buffer"));
        return;
    }
    if(
        destResource != srcResource
        && dest.m_buffer != VK_NULL_HANDLE
        && dest.m_buffer == src.m_buffer
    ){
        rejectCommandRecording(
            NWB_TEXT("copy buffer"),
            NWB_TEXT("distinct buffer objects alias the same native buffer")
        );
        return;
    }

    const bool sameNativeBuffer = dest.m_buffer == src.m_buffer;
    const ResourceStates::Mask sourceState = sameNativeBuffer
        ? ResourceStates::CopySource | ResourceStates::CopyDest
        : ResourceStates::CopySource
    ;
    const ResourceStates::Mask destinationState = sameNativeBuffer
        ? ResourceStates::CopySource | ResourceStates::CopyDest
        : ResourceStates::CopyDest
    ;
    if(!validateBufferForGpuState(srcResource, sourceState, NWB_TEXT("copy buffer")))
        return;
    if(
        destResource != srcResource
        && !validateBufferForGpuState(destResource, destinationState, NWB_TEXT("copy buffer"))
    )
        return;

    endActiveRenderPass();
    if(sameNativeBuffer)
        setBufferState(srcResource, sourceState);
    else{
        setBufferState(srcResource, sourceState);
        if(!m_commandRecordingFailed)
            setBufferState(destResource, destinationState);
    }
    if(m_commandRecordingFailed)
        return;

    VkBufferCopy region{};
    region.srcOffset = srcOffsetBytes;
    region.dstOffset = destOffsetBytes;
    region.size = dataSizeBytes;

    vkCmdCopyBuffer(m_currentCmdBuf->m_cmdBuf, src.m_buffer, dest.m_buffer, 1, &region);

    retainResource(srcResource);
    retainResource(destResource);
}

bool CommandList::recordPreflightedCopyBufferDirectVulkan(
    Buffer* const destResource,
    const u64 destOffsetBytes,
    Buffer* const srcResource,
    const u64 srcOffsetBytes,
    const u64 dataSizeBytes
){
    // This is intentionally narrower than copyBuffer: command-IR replay has already validated every operand
    // against the compiled graph, while graph packet recording has already made the CopySource/CopyDest states
    // authoritative. Keep the direct lowerer from silently reintroducing per-command state tracking.
    if(dataSizeBytes == 0u)
        return false;
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Transfer, NWB_TEXT("direct command-IR copy buffer")))
        return false;
    if(!VulkanDetail::AreAllPointersValid(destResource, srcResource)){
        rejectCommandRecording(NWB_TEXT("direct command-IR copy buffer"), NWB_TEXT("source or destination buffer is null"));
        return false;
    }

    Buffer& dest = *destResource;
    Buffer& src = *srcResource;
    const BufferDesc& destDesc = dest.getDescription();
    const BufferDesc& srcDesc = src.getDescription();
    if(!VulkanDetail::IsBufferRangeInBounds(destDesc, destOffsetBytes, dataSizeBytes)){
        rejectCommandRecording(NWB_TEXT("direct command-IR copy buffer"), NWB_TEXT("destination range is outside the buffer"));
        return false;
    }
    if(!VulkanDetail::IsBufferRangeInBounds(srcDesc, srcOffsetBytes, dataSizeBytes)){
        rejectCommandRecording(NWB_TEXT("direct command-IR copy buffer"), NWB_TEXT("source range is outside the buffer"));
        return false;
    }
    if(
        dest.m_buffer == src.m_buffer
        && VulkanDetail::BufferRangesOverlap(destOffsetBytes, dataSizeBytes, srcOffsetBytes, dataSizeBytes)
    ){
        rejectCommandRecording(
            NWB_TEXT("direct command-IR copy buffer"),
            NWB_TEXT("source and destination ranges overlap in the same buffer")
        );
        return false;
    }
    if(
        destResource != srcResource
        && dest.m_buffer != VK_NULL_HANDLE
        && dest.m_buffer == src.m_buffer
    ){
        rejectCommandRecording(
            NWB_TEXT("direct command-IR copy buffer"),
            NWB_TEXT("distinct buffer objects alias the same native buffer")
        );
        return false;
    }

    const bool sameNativeBuffer = dest.m_buffer == src.m_buffer;
    const ResourceStates::Mask sourceState = sameNativeBuffer
        ? ResourceStates::CopySource | ResourceStates::CopyDest
        : ResourceStates::CopySource
    ;
    const ResourceStates::Mask destinationState = sameNativeBuffer
        ? ResourceStates::CopySource | ResourceStates::CopyDest
        : ResourceStates::CopyDest
    ;
    if(
        !validateBufferForGpuState(
            srcResource,
            sourceState,
            NWB_TEXT("direct command-IR copy buffer")
        )
    )
        return false;
    if(
        destResource != srcResource
        && !validateBufferForGpuState(
            destResource,
            destinationState,
            NWB_TEXT("direct command-IR copy buffer")
        )
    )
        return false;

    endActiveRenderPass();
    if(m_commandRecordingFailed)
        return false;
    registerHostReadbackBuffer(dest);

    VkBufferCopy region{};
    region.srcOffset = srcOffsetBytes;
    region.dstOffset = destOffsetBytes;
    region.size = dataSizeBytes;
    vkCmdCopyBuffer(m_currentCmdBuf->m_cmdBuf, src.m_buffer, dest.m_buffer, 1u, &region);

    retainResource(srcResource);
    retainResource(destResource);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

