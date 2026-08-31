// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "buffer_resource_detail.h"

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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_buffer{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using BufferQueueFamilyVector = Vector<u32, Alloc::GlobalArena>;

[[nodiscard]] static BufferQueueFamilyVector CopyBufferQueueFamilyIndices(
    const VulkanContext& context,
    const VkBufferCreateInfo& bufferInfo
){
    BufferQueueFamilyVector result(context.objectArena);
    if(bufferInfo.queueFamilyIndexCount == 0u)
        return result;

    NWB_ASSERT(bufferInfo.pQueueFamilyIndices != nullptr);
    if(!bufferInfo.pQueueFamilyIndices)
        return result;
    result.assign(
        bufferInfo.pQueueFamilyIndices,
        bufferInfo.pQueueFamilyIndices + bufferInfo.queueFamilyIndexCount
    );
    return result;
}

[[nodiscard]] static VkBufferCreateInfo RetainBufferCreateInfo(
    const VkBufferCreateInfo& bufferInfo,
    const BufferQueueFamilyVector& queueFamilyIndices
){
    NWB_ASSERT(queueFamilyIndices.size() <= Limit<u32>::s_Max);
    VkBufferCreateInfo result = bufferInfo;
    result.pNext = nullptr;
    result.queueFamilyIndexCount = static_cast<u32>(queueFamilyIndices.size());
    result.pQueueFamilyIndices = queueFamilyIndices.empty() ? nullptr : queueFamilyIndices.data();
    return result;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Buffer::Buffer(
    const VulkanContext& context,
    VulkanAllocator& allocator,
    const BufferDesc& creationDesc,
    const VkBufferCreateInfo& bufferInfo,
    const bool initialStateKnown
)
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_desc(creationDesc)
    , m_creationDesc(creationDesc)
    , m_creationInitialStateKnown(initialStateKnown)
    , m_bufferQueueFamilyIndices(__hidden_buffer::CopyBufferQueueFamilyIndices(context, bufferInfo))
    , m_bufferInfo(__hidden_buffer::RetainBufferCreateInfo(bufferInfo, m_bufferQueueFamilyIndices))
    , m_retainedStateKnown(creationDesc.keepInitialState && initialStateKnown)
    , m_versionTracking(context.objectArena)
    , m_bufferViews(context.objectArena)
    , m_context(context)
    , m_allocator(allocator)
{}
Buffer::~Buffer(){
    const VkBuffer registeredNativeBuffer = m_buffer;

    for(auto& viewEntry : m_bufferViews){
        if(viewEntry.view != VK_NULL_HANDLE){
            m_context.deviceDispatch.vkDestroyBufferView(m_context.device, viewEntry.view, m_context.allocationCallbacks);
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
                    m_context.deviceDispatch.vkDestroyBuffer(m_context.device, m_buffer, m_context.allocationCallbacks);
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
            if(m_creationDesc.isVirtual){
                if(m_buffer != VK_NULL_HANDLE){
                    m_context.deviceDispatch.vkDestroyBuffer(m_context.device, m_buffer, m_context.allocationCallbacks);
                    m_buffer = VK_NULL_HANDLE;
                }
            }
            else
                m_allocator.destroyBuffer(*this);
        }
    }

    m_allocator.unregisterBufferNativeIdentity(registeredNativeBuffer, *this);
}

bool Buffer::descriptionMatchesCreation()const noexcept{
    return VulkanBufferDetail::BufferDescriptionsEqual(m_desc, m_creationDesc);
}

ResourceStates::Mask Buffer::resolveTaskGraphImportInitialState()const noexcept{
    if(!m_creationDesc.keepInitialState){
        return m_managed || m_creationInitialStateKnown
            ? m_creationDesc.initialState
            : ResourceStates::Unknown
        ;
    }
    return isRetainedStateKnown() ? m_creationDesc.initialState : ResourceStates::Unknown;
}

Object Buffer::getNativeHandle(ObjectType objectType){
    if(objectType != ObjectTypes::VK_Buffer)
        return nullptr;

    return Object(m_buffer);
}

bool Buffer::isRetainedStateKnown()const noexcept{
    return m_retainedStateKnown.load(MemoryOrder::acquire);
}

void Buffer::setRetainedStateKnown(const bool known)noexcept{
    if(m_creationDesc.keepInitialState)
        m_retainedStateKnown.store(known, MemoryOrder::release);
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

    if(byteOffset >= m_creationDesc.byteSize)
        return VK_NULL_HANDLE;

    const u64 maxRange = m_creationDesc.byteSize - byteOffset;
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
    res = m_context.deviceDispatch.vkCreateBufferView(m_context.device, &viewInfo, m_context.allocationCallbacks, &view);
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

    if(!ResourceQueueSharing::IsValid(d.queueSharing)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer: queue sharing contains unknown bits"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create buffer: queue sharing contains unknown bits"));
        return nullptr;
    }
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
    if(!VulkanBufferDetail::IsBufferCreationStateMaskValid(d.initialState)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer: initial state is invalid for a buffer"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create buffer: invalid initial state"));
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

    const VkBufferUsageFlags usageFlags = VulkanBufferDetail::PickManagedBufferUsage(m_context, d);
    if(!VulkanBufferDetail::IsBufferUsageCompatibleWithResourceStates(d, usageFlags, d.initialState)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer: initial state requires an undeclared buffer usage"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create buffer: initial state is incompatible with its description"));
        return nullptr;
    }
    if(!VulkanBufferDetail::IsBufferUsageSupportedByDevice(m_context, usageFlags)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer: required usage is unsupported by the device"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create buffer: required usage is unsupported by the device"));
        return nullptr;
    }
    if(d.isShaderBindingTable){
        if(!m_context.extensions.KHR_ray_tracing_pipeline || !m_context.extensions.buffer_device_address){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create shader binding table buffer: ray tracing pipeline and buffer device address support are required"));
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to create shader binding table buffer: ray tracing pipeline and buffer device address support are required"));
            return nullptr;
        }

    }

    u64 size = d.byteSize;

    if(d.isVolatile){
        const u64 alignment = Max<u64>(m_context.physicalDeviceProperties.limits.minUniformBufferOffsetAlignment, 1u);
        u64 alignedSize = 0;
        if(!AlignUpU64Checked(size, alignment, alignedSize)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create volatile buffer: aligned size overflows"));
            return nullptr;
        }
        if(alignedSize > Limit<u64>::s_Max / d.maxVersions){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create volatile buffer: versioned size overflows"));
            return nullptr;
        }
        size = alignedSize * d.maxVersions;
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usageFlags;
    const QueueFamilySharingInfo sharingInfo = ResolveQueueFamilySharing(d.queueSharing, m_context);
    bufferInfo.sharingMode = sharingInfo.mode;
    bufferInfo.queueFamilyIndexCount = sharingInfo.familyIndexCount;
    bufferInfo.pQueueFamilyIndices = sharingInfo.data();

    auto* buffer = NewArenaObject<Buffer>(m_context.objectArena, m_context, m_allocator, d, bufferInfo, true);
    if(d.isVolatile)
        buffer->m_versionTracking.resize(d.maxVersions);

    if(d.isVirtual)
        res = m_context.deviceDispatch.vkCreateBuffer(m_context.device, &bufferInfo, m_context.allocationCallbacks, &buffer->m_buffer);
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
        if(usageFlags & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT){
            VkBufferDeviceAddressInfo addressInfo{};
            addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            addressInfo.buffer = buffer->m_buffer;
            buffer->m_deviceAddress = m_context.deviceDispatch.vkGetBufferDeviceAddress(m_context.device, &addressInfo);
        }
    }

    return BufferHandle(buffer, BufferHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

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

    const u64 completedUploadVersion = m_device.queueGetCompletedInstance(m_creationDesc.physicalQueue);
    if(!uploadMgr.suballocateBuffer(
        static_cast<u64>(dataSize),
        &outStagingBuffer,
        &outStagingOffset,
        &outCpuVA,
        m_currentCmdBuf.get(),
        m_nativeRecordingID,
        m_creationDesc.physicalQueue,
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
    const BufferDesc& desc = buffer.m_creationDesc;
    if(!VulkanDetail::IsBufferRangeInBounds(desc, destOffsetBytes, static_cast<u64>(dataSize))){
        rejectCommandRecording(NWB_TEXT("write buffer"), NWB_TEXT("destination range is outside the buffer"));
        return false;
    }
    if((destOffsetBytes & s_BufferAlignmentMask) != 0u || (dataSize & s_BufferAlignmentMask) != 0u){
        rejectCommandRecording(NWB_TEXT("write buffer"), NWB_TEXT("copy offset and size must be 4-byte aligned"));
        return false;
    }
    if(!validateBufferForGpuState(
        bufferResource,
        ResourceStates::CopyDest,
        NWB_TEXT("write buffer"),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT
    ))
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

    m_context.deviceDispatch.vkCmdCopyBuffer(m_currentCmdBuf->m_cmdBuf, stagingBuffer->m_buffer, buffer.m_buffer, 1, &region);

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
    if((buffer.m_creationDesc.byteSize & s_BufferAlignmentMask) != 0u){
        rejectCommandRecording(NWB_TEXT("clear buffer"), NWB_TEXT("buffer size is not 4-byte aligned"));
        return;
    }
    if(!validateBufferForGpuState(
        bufferResource,
        ResourceStates::CopyDest,
        NWB_TEXT("clear buffer"),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT
    ))
        return;

    endActiveRenderPass();
    setBufferState(bufferResource, ResourceStates::CopyDest);
    if(m_commandRecordingFailed)
        return;

    m_context.deviceDispatch.vkCmdFillBuffer(m_currentCmdBuf->m_cmdBuf, buffer.m_buffer, 0, VK_WHOLE_SIZE, clearValue);
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

    const BufferDesc& destDesc = dest.m_creationDesc;
    const BufferDesc& srcDesc = src.m_creationDesc;

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
    const VkBufferUsageFlags sourceUsage = sameNativeBuffer
        ? VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
        : VK_BUFFER_USAGE_TRANSFER_SRC_BIT
    ;
    if(!validateBufferForGpuState(srcResource, sourceState, NWB_TEXT("copy buffer"), sourceUsage))
        return;
    if(
        destResource != srcResource
        && !validateBufferForGpuState(
            destResource,
            destinationState,
            NWB_TEXT("copy buffer"),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT
        )
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

    m_context.deviceDispatch.vkCmdCopyBuffer(m_currentCmdBuf->m_cmdBuf, src.m_buffer, dest.m_buffer, 1, &region);

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
    const BufferDesc& destDesc = dest.m_creationDesc;
    const BufferDesc& srcDesc = src.m_creationDesc;
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
            NWB_TEXT("direct command-IR copy buffer"),
            sameNativeBuffer
                ? VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                : VK_BUFFER_USAGE_TRANSFER_SRC_BIT
        )
    )
        return false;
    if(
        destResource != srcResource
        && !validateBufferForGpuState(
            destResource,
            destinationState,
            NWB_TEXT("direct command-IR copy buffer"),
            VK_BUFFER_USAGE_TRANSFER_DST_BIT
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
    m_context.deviceDispatch.vkCmdCopyBuffer(m_currentCmdBuf->m_cmdBuf, src.m_buffer, dest.m_buffer, 1u, &region);

    retainResource(srcResource);
    retainResource(destResource);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

