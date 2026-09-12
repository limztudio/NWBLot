// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "resource_bindings_detail.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

DescriptorBufferManager::DescriptorBufferManager(Device& device, const VulkanContext& context, VulkanAllocator& allocator)
    : m_device(device)
    , m_context(context)
    , m_allocator(allocator)
    , m_resourceSegment(context.objectArena, VulkanDetail::AllocateDescriptorBufferStorageIdentity())
    , m_samplerSegment(context.objectArena, VulkanDetail::AllocateDescriptorBufferStorageIdentity())
{}
DescriptorBufferManager::~DescriptorBufferManager()noexcept{
    shutdownForDeviceTeardown();
}

bool DescriptorBufferManager::shutdownForLifecycleOperation(VkResult& outIdleResult){
    outIdleResult = VK_SUCCESS;
    {
        ScopedLock lifecycleLock(m_lifecycleMutex);
        if(m_lifecycleTransitioning){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor-buffer shutdown rejected during another lifecycle transition."));
            return false;
        }
        if(
            m_resourceSegment.buffer == VK_NULL_HANDLE
            && m_samplerSegment.buffer == VK_NULL_HANDLE
            && !m_resourceSegment.allocation
            && !m_samplerSegment.allocation
        ){
            m_bindingGeneration = 0u;
            m_enabled = false;
            return true;
        }

        m_lifecycleTransitioning = true;
        m_bindingGeneration = 0u;
        m_enabled = false;
    }

    outIdleResult = m_device.waitForNativeIdle();
    if(outIdleResult == VK_ERROR_DEVICE_LOST)
        m_device.markDeviceLost();
    if(outIdleResult != VK_SUCCESS && outIdleResult != VK_ERROR_DEVICE_LOST){
        {
            ScopedLock lifecycleLock(m_lifecycleMutex);

            m_lifecycleTransitioning = false;
        }
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor-buffer shutdown is refusing to destroy storage after device-idle wait failed."));
        return false;
    }

    ScopedLock lifecycleLock(m_lifecycleMutex);
    ScopedLock resourceLock(m_resourceSegment.mutex);
    ScopedLock samplerLock(m_samplerSegment.mutex);

    shutdownSegment(m_resourceSegment);
    shutdownSegment(m_samplerSegment);
    m_lifecycleTransitioning = false;
    return true;
}

bool DescriptorBufferManager::initialize(){
    UniqueLock<Futex> operationLock(m_lifecycleOperationMutex);

    VkResult idleResult = VK_SUCCESS;
    if(!shutdownForLifecycleOperation(idleResult))
        return false;
    if(idleResult == VK_ERROR_DEVICE_LOST){
        operationLock.unlock();
        m_device.captureDeviceLoss("descriptor-buffer initialization idle");
        return false;
    }

    ScopedLock lifecycleLock(m_lifecycleMutex);
    if(m_lifecycleTransitioning){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor-buffer initialization rejected during another lifecycle transition."));
        return false;
    }
    m_lifecycleTransitioning = true;
    const auto failInitialization = [this](){
        shutdownSegment(m_resourceSegment);
        shutdownSegment(m_samplerSegment);
        m_bindingGeneration = 0u;
        m_enabled = false;
        m_lifecycleTransitioning = false;
        return false;
    };

    if(!VulkanDetail::HasDescriptorBufferStartupPrerequisites(
        VulkanDetail::QueryDescriptorBufferStartupPrerequisites(m_context)
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor-buffer startup prerequisites are unavailable."));
        return failInitialization();
    }
    if(m_resourceSegment.storageIdentity == 0u || m_samplerSegment.storageIdentity == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor-buffer storage identity space is exhausted."));
        return failInitialization();
    }
    if(m_nextBindingGeneration == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor-buffer binding generation space is exhausted."));
        return failInitialization();
    }

    const auto& props = m_context.descriptorBufferProperties;

    // Cap persistent mapped segment reservations at practical working sizes.
    constexpr u32 s_TargetResourceSegmentBytes = 32u * 1024u * 1024u;
    constexpr u32 s_TargetSamplerSegmentBytes = 2u * 1024u * 1024u;

    if(props.descriptorBufferOffsetAlignment == 0 || props.descriptorBufferOffsetAlignment > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer offset alignment is outside the supported 32-bit range."));
        return failInitialization();
    }

    const u32 offsetAlignment = static_cast<u32>(props.descriptorBufferOffsetAlignment);
    if(
        props.maxDescriptorBufferBindings < DescriptorBufferManager::s_PersistentDescriptorBufferCount
        || props.maxResourceDescriptorBufferBindings == 0u
        || props.maxSamplerDescriptorBufferBindings == 0u
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer limits cannot bind the required resource and sampler segments."));
        return failInitialization();
    }

    const VkDeviceSize resourceMaxBytes = Min<VkDeviceSize>(props.resourceDescriptorBufferAddressSpaceSize, props.maxResourceDescriptorBufferRange);
    const VkDeviceSize samplerMaxBytes = Min<VkDeviceSize>(props.samplerDescriptorBufferAddressSpaceSize, props.maxSamplerDescriptorBufferRange);

    const auto makeCapacity = [&](const VkDeviceSize maximumBytes, const u32 targetBytes, u32& outCapacityBytes) -> bool{
        const VkDeviceSize cappedBytes = Min<VkDeviceSize>(maximumBytes, targetBytes);
        const VkDeviceSize alignedBytes = cappedBytes - (cappedBytes % offsetAlignment);
        if(alignedBytes == 0u)
            return false;
        outCapacityBytes = static_cast<u32>(alignedBytes);
        return true;
    };

    u32 resourceCapacityBytes = 0;
    u32 samplerCapacityBytes = 0;
    if(
        !makeCapacity(resourceMaxBytes, s_TargetResourceSegmentBytes, resourceCapacityBytes)
        || !makeCapacity(samplerMaxBytes, s_TargetSamplerSegmentBytes, samplerCapacityBytes)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer properties do not allow aligned global segments."));
        return failInitialization();
    }

    const VkDeviceSize totalCapacityBytes = static_cast<VkDeviceSize>(resourceCapacityBytes) + samplerCapacityBytes;
    if(totalCapacityBytes > props.descriptorBufferAddressSpaceSize){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer global address space {} cannot hold the requested {} bytes of resource and sampler segments.")
            , props.descriptorBufferAddressSpaceSize
            , totalCapacityBytes
        );
        return failInitialization();
    }

    if(!initializeSegment(m_resourceSegment, "vk_resource_descriptor_buffer", resourceCapacityBytes))
        return failInitialization();

    if(!initializeSegment(m_samplerSegment, "vk_sampler_descriptor_buffer", samplerCapacityBytes))
        return failInitialization();

    m_bindingGeneration = m_nextBindingGeneration;
    m_nextBindingGeneration = m_bindingGeneration == UINT64_MAX ? 0u : m_bindingGeneration + 1u;
    m_enabled = true;
    m_lifecycleTransitioning = false;
    return true;
}

bool DescriptorBufferManager::shutdown(){
    UniqueLock<Futex> operationLock(m_lifecycleOperationMutex);

    VkResult idleResult = VK_SUCCESS;
    const bool result = shutdownForLifecycleOperation(idleResult);
    operationLock.unlock();
    if(idleResult == VK_ERROR_DEVICE_LOST)
        m_device.captureDeviceLoss("descriptor-buffer shutdown idle");
    return result;
}

void DescriptorBufferManager::shutdownForDeviceTeardown()noexcept{
    NothrowScopedLock operationLock(m_lifecycleOperationMutex);
    NothrowScopedLock lifecycleLock(m_lifecycleMutex);
    NothrowScopedLock resourceLock(m_resourceSegment.mutex);
    NothrowScopedLock samplerLock(m_samplerSegment.mutex);

    m_bindingGeneration = 0u;
    m_enabled = false;
    shutdownSegment(m_resourceSegment);
    shutdownSegment(m_samplerSegment);
    m_lifecycleTransitioning = false;
}

bool DescriptorBufferManager::isEnabled()const{
    ScopedLock lifecycleLock(m_lifecycleMutex);

    return m_enabled && !m_lifecycleTransitioning && m_bindingGeneration != 0u;
}

u32 DescriptorBufferManager::getDescriptorSize(const VkDescriptorType descriptorType)const{
    ScopedLock lifecycleLock(m_lifecycleMutex);

    return VulkanDetail::GetDescriptorSize(m_context, m_enabled, descriptorType);
}

u32 DescriptorBufferManager::getOffsetAlignmentBytes()const{
    return VulkanDetail::GetDescriptorBufferOffsetAlignmentBytes(m_context);
}

u64 DescriptorBufferManager::getUniformBufferAddressAlignmentBytes()const{
    return Max<u64>(m_context.physicalDeviceProperties.limits.minUniformBufferOffsetAlignment, 1u);
}

u64 DescriptorBufferManager::getStorageBufferAddressAlignmentBytes()const{
    return Max<u64>(m_context.physicalDeviceProperties.limits.minStorageBufferOffsetAlignment, 1u);
}

u64 DescriptorBufferManager::getTexelBufferAddressAlignmentBytes()const{
    return Max<u64>(m_context.physicalDeviceProperties.limits.minTexelBufferOffsetAlignment, 1u);
}

u32 DescriptorBufferManager::getMaxTexelBufferElements()const{
    return m_context.physicalDeviceProperties.limits.maxTexelBufferElements;
}

VkDescriptorBufferBindingInfoEXT DescriptorBufferManager::getResourceBindingInfo()const{
    ScopedLock lifecycleLock(m_lifecycleMutex);

    return m_enabled ? m_resourceSegment.bindingInfo : VkDescriptorBufferBindingInfoEXT{};
}

VkDescriptorBufferBindingInfoEXT DescriptorBufferManager::getSamplerBindingInfo()const{
    ScopedLock lifecycleLock(m_lifecycleMutex);

    return m_enabled ? m_samplerSegment.bindingInfo : VkDescriptorBufferBindingInfoEXT{};
}

DescriptorBufferSegment DescriptorBufferManager::allocate(const DescriptorBufferSegmentKind::Enum kind, const u32 sizeBytes, const u32 alignmentBytes){
    return allocateForBindingGeneration(kind, sizeBytes, alignmentBytes, 0u);
}

DescriptorBufferSegment DescriptorBufferManager::allocateForBindingGeneration(
    const DescriptorBufferSegmentKind::Enum kind,
    const u32 sizeBytes,
    const u32 alignmentBytes,
    const u64 requiredGeneration
){
    DescriptorBufferSegment result{};
    ScopedLock lifecycleLock(m_lifecycleMutex);

    if(requiredGeneration != 0u && requiredGeneration != m_bindingGeneration)
        return result;
    if(!m_enabled || sizeBytes == 0)
        return result;
    if(kind != DescriptorBufferSegmentKind::Resource && kind != DescriptorBufferSegmentKind::Sampler){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer allocation rejected: invalid segment kind {}."), static_cast<u32>(kind));
        return result;
    }

    const u32 requiredAlignmentBytes = getOffsetAlignmentBytes();
    if(alignmentBytes == 0u || (alignmentBytes % requiredAlignmentBytes) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer allocation rejected: alignment {} is not a non-zero multiple of required alignment {}.")
            , alignmentBytes
            , requiredAlignmentBytes
        );
        return result;
    }

    SegmentStorage& segment = kind == DescriptorBufferSegmentKind::Sampler ? m_samplerSegment : m_resourceSegment;
    auto clearAllocation = [&](const DescriptorBufferSegment& allocation){
        if(allocation.valid() && segment.mappedMemory)
            NWB_MEMSET(static_cast<u8*>(segment.mappedMemory) + allocation.offsetBytes, 0, allocation.sizeBytes);
    };
    auto finishAllocation = [&](const u32 offsetBytes) -> DescriptorBufferSegment{
        result.kind = kind;
        result.offsetBytes = offsetBytes;
        result.sizeBytes = sizeBytes;
        result.storageIdentity = segment.storageIdentity;
        result.allocationSerial = segment.nextAllocationSerial++;
        clearAllocation(result);
        // Ordered live ranges allow binary-search ownership checks.
        usize insertIndex = 0u;
        while(
            insertIndex < segment.liveAllocations.size()
            && segment.liveAllocations[insertIndex].offsetBytes < result.offsetBytes
        )
            ++insertIndex;
        segment.liveAllocations.insert(segment.liveAllocations.begin() + insertIndex, result);
        return result;
    };

    ScopedLock lock(segment.mutex);

    if(segment.nextAllocationSerial == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer allocation rejected: allocation serial space is exhausted."));
        return result;
    }

    for(usize i = 0; i < segment.freeRanges.size(); ++i){
        FreeRange range = segment.freeRanges[i];
        if(range.sizeBytes > UINT32_MAX - range.offsetBytes)
            continue;

        u32 alignedOffset = 0;
        if(!AlignUpU32Checked(range.offsetBytes, alignmentBytes, alignedOffset))
            continue;

        const u32 rangeEnd = range.offsetBytes + range.sizeBytes;
        if(alignedOffset >= rangeEnd)
            continue;

        const u32 consumedPrefix = alignedOffset - range.offsetBytes;
        const u32 remainingBytes = range.sizeBytes - consumedPrefix;
        if(remainingBytes < sizeBytes)
            continue;
        if(sizeBytes > UINT32_MAX - alignedOffset)
            continue;

        const u32 allocEnd = alignedOffset + sizeBytes;
        if(consumedPrefix > 0){
            segment.freeRanges[i] = { range.offsetBytes, consumedPrefix };
            if(allocEnd < rangeEnd)
                segment.freeRanges.insert(segment.freeRanges.begin() + i + 1u, { allocEnd, rangeEnd - allocEnd });
        }
        else if(allocEnd < rangeEnd){
            segment.freeRanges[i] = { allocEnd, rangeEnd - allocEnd };
        }
        else{
            segment.freeRanges.erase(segment.freeRanges.begin() + i);
        }

        return finishAllocation(alignedOffset);
    }

    u32 alignedOffset = 0;
    if(!AlignUpU32Checked(segment.writableOffsetBytes, alignmentBytes, alignedOffset)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer alignment overflows 32-bit offsets."));
        return result;
    }
    if(alignedOffset > segment.capacityBytes || sizeBytes > segment.capacityBytes - alignedOffset){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer is out of space (kind={}, requested={} bytes).")
            , kind == DescriptorBufferSegmentKind::Sampler ? NWB_TEXT("sampler") : NWB_TEXT("resource")
            , sizeBytes
        );
        return result;
    }

    segment.writableOffsetBytes = alignedOffset + sizeBytes;
    return finishAllocation(alignedOffset);
}

void DescriptorBufferManager::free(const DescriptorBufferSegment& segment){
    freeForBindingGeneration(segment, 0u);
}

void DescriptorBufferManager::freeForBindingGeneration(
    const DescriptorBufferSegment& segment,
    const u64 requiredGeneration
){
    ScopedLock lifecycleLock(m_lifecycleMutex);

    if(requiredGeneration != 0u && requiredGeneration != m_bindingGeneration)
        return;
    if(!m_enabled || segment.sizeBytes == 0u)
        return;
    if(segment.kind != DescriptorBufferSegmentKind::Resource && segment.kind != DescriptorBufferSegmentKind::Sampler){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer free rejected: invalid segment kind {}."), static_cast<u32>(segment.kind));
        return;
    }
    if(segment.storageIdentity == 0u || segment.allocationSerial == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer free rejected: allocation identity is invalid."));
        return;
    }

    SegmentStorage& storage = segment.kind == DescriptorBufferSegmentKind::Sampler ? m_samplerSegment : m_resourceSegment;

    ScopedLock lock(storage.mutex);

    if(segment.storageIdentity != storage.storageIdentity){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer free rejected: allocation belongs to another storage."));
        return;
    }

    if(
        segment.offsetBytes > storage.capacityBytes
        || segment.sizeBytes > storage.capacityBytes - segment.offsetBytes
        || segment.offsetBytes > storage.writableOffsetBytes
        || segment.sizeBytes > storage.writableOffsetBytes - segment.offsetBytes
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer free rejected: range {} + {} is outside the live segment."), segment.offsetBytes, segment.sizeBytes);
        return;
    }

    usize allocationIndex = 0u;
    while(
        allocationIndex < storage.liveAllocations.size()
        && storage.liveAllocations[allocationIndex].offsetBytes < segment.offsetBytes
    )
        ++allocationIndex;
    if(
        allocationIndex == storage.liveAllocations.size()
        || storage.liveAllocations[allocationIndex].kind != segment.kind
        || storage.liveAllocations[allocationIndex].offsetBytes != segment.offsetBytes
        || storage.liveAllocations[allocationIndex].sizeBytes != segment.sizeBytes
        || storage.liveAllocations[allocationIndex].storageIdentity != segment.storageIdentity
        || storage.liveAllocations[allocationIndex].allocationSerial != segment.allocationSerial
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer free rejected: range {} + {} is not a live allocation."), segment.offsetBytes, segment.sizeBytes);
        return;
    }

    const auto rangeEnd = [](const FreeRange& range, u32& outEnd) -> bool{
        if(range.sizeBytes > UINT32_MAX - range.offsetBytes)
            return false;
        outEnd = range.offsetBytes + range.sizeBytes;
        return true;
    };

    FreeRange freedRange{ segment.offsetBytes, segment.sizeBytes };
    for(const FreeRange& range : storage.freeRanges){
        u32 rangeEndBytes = 0u;
        if(
            !rangeEnd(range, rangeEndBytes)
            || (
                freedRange.offsetBytes < rangeEndBytes
                && range.offsetBytes < freedRange.offsetBytes + freedRange.sizeBytes
            )
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer free rejected: range {} + {} overlaps a free range."), segment.offsetBytes, segment.sizeBytes);
            return;
        }
    }

    storage.liveAllocations.erase(storage.liveAllocations.begin() + allocationIndex);
    usize insertIndex = 0u;
    while(insertIndex < storage.freeRanges.size() && storage.freeRanges[insertIndex].offsetBytes < freedRange.offsetBytes)
        ++insertIndex;

    storage.freeRanges.insert(storage.freeRanges.begin() + insertIndex, freedRange);

    const auto mergeAdjacentAt = [&](const usize leftIndex) -> bool{
        if(leftIndex + 1u >= storage.freeRanges.size())
            return false;

        FreeRange& left = storage.freeRanges[leftIndex];
        const FreeRange right = storage.freeRanges[leftIndex + 1u];

        u32 leftEnd = 0;
        if(!rangeEnd(left, leftEnd) || leftEnd != right.offsetBytes || right.sizeBytes > UINT32_MAX - left.sizeBytes)
            return false;

        left.sizeBytes += right.sizeBytes;
        storage.freeRanges.erase(storage.freeRanges.begin() + leftIndex + 1u);
        return true;
    };

    if(mergeAdjacentAt(insertIndex))
        mergeAdjacentAt(insertIndex);
    if(insertIndex > 0)
        mergeAdjacentAt(insertIndex - 1u);
}

bool DescriptorBufferManager::captureBindingSnapshotLocked(BindingSnapshot& outSnapshot)const{
    outSnapshot = {};
    const u32 offsetAlignmentBytes = VulkanDetail::GetDescriptorBufferOffsetAlignmentBytes(m_context);
    if(
        !m_enabled
        || m_lifecycleTransitioning
        || m_bindingGeneration == 0u
        || offsetAlignmentBytes == 0u
        || m_resourceSegment.storageIdentity == 0u
        || m_samplerSegment.storageIdentity == 0u
        || m_resourceSegment.storageIdentity == m_samplerSegment.storageIdentity
        || m_resourceSegment.buffer == VK_NULL_HANDLE
        || m_samplerSegment.buffer == VK_NULL_HANDLE
        || !m_resourceSegment.allocation
        || !m_samplerSegment.allocation
        || !m_resourceSegment.mappedMemory
        || !m_samplerSegment.mappedMemory
        || m_resourceSegment.deviceAddress == 0u
        || m_samplerSegment.deviceAddress == 0u
        || (m_resourceSegment.deviceAddress % offsetAlignmentBytes) != 0u
        || (m_samplerSegment.deviceAddress % offsetAlignmentBytes) != 0u
        || m_resourceSegment.capacityBytes == 0u
        || m_samplerSegment.capacityBytes == 0u
        || m_resourceSegment.bindingInfo.sType != VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT
        || m_samplerSegment.bindingInfo.sType != VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT
        || m_resourceSegment.bindingInfo.address != m_resourceSegment.deviceAddress
        || m_samplerSegment.bindingInfo.address != m_samplerSegment.deviceAddress
        || m_resourceSegment.bindingInfo.usage != VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT
        || m_samplerSegment.bindingInfo.usage != VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT
    )
        return false;

    outSnapshot.bindingInfos[s_ResourceDescriptorBufferIndex] = m_resourceSegment.bindingInfo;
    outSnapshot.bindingInfos[s_SamplerDescriptorBufferIndex] = m_samplerSegment.bindingInfo;
    outSnapshot.generation = m_bindingGeneration;
    outSnapshot.resourceStorageIdentity = m_resourceSegment.storageIdentity;
    outSnapshot.samplerStorageIdentity = m_samplerSegment.storageIdentity;
    outSnapshot.offsetAlignmentBytes = offsetAlignmentBytes;
    return true;
}

bool DescriptorBufferManager::isLiveSegmentLocked(
    const SegmentStorage& storage,
    const DescriptorBufferSegment& segment,
    const DescriptorBufferSegmentKind::Enum expectedKind
)const{
    if(
        !segment.valid()
        || segment.kind != expectedKind
        || segment.storageIdentity != storage.storageIdentity
        || segment.offsetBytes > storage.capacityBytes
        || segment.sizeBytes > storage.capacityBytes - segment.offsetBytes
        || segment.offsetBytes > storage.writableOffsetBytes
        || segment.sizeBytes > storage.writableOffsetBytes - segment.offsetBytes
    )
        return false;

    usize first = 0u;
    usize last = storage.liveAllocations.size();
    while(first < last){
        const usize middle = first + (last - first) / 2u;
        if(storage.liveAllocations[middle].offsetBytes < segment.offsetBytes)
            first = middle + 1u;
        else
            last = middle;
    }
    if(first == storage.liveAllocations.size())
        return false;

    const DescriptorBufferSegment& liveSegment = storage.liveAllocations[first];
    return liveSegment.kind == segment.kind
        && liveSegment.offsetBytes == segment.offsetBytes
        && liveSegment.sizeBytes == segment.sizeBytes
        && liveSegment.storageIdentity == segment.storageIdentity
        && liveSegment.allocationSerial == segment.allocationSerial
    ;
}

bool DescriptorBufferManager::initializeSegment(SegmentStorage& segment, const ACompactString& debugName, const u32 capacityBytes){
    VkResult res = VK_SUCCESS;

    shutdownSegment(segment);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = capacityBytes;
    bufferInfo.usage =
        (&segment == &m_resourceSegment ? VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT : VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT)
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    const QueueFamilySharingInfo sharingInfo = ResolveQueueFamilySharing(ResourceQueueSharing::GraphicsAndAsyncCompute, m_context);
    bufferInfo.sharingMode = sharingInfo.mode;
    bufferInfo.queueFamilyIndexCount = sharingInfo.familyIndexCount;
    bufferInfo.pQueueFamilyIndices = sharingInfo.data();

    res = m_allocator.createHostMappedBuffer(
        segment.buffer,
        segment.allocation,
        segment.mappedMemory,
        bufferInfo
    );
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create descriptor buffer '{}': {}")
            , StringConvert(debugName.view())
            , ResultToString(res)
        );
        return false;
    }
    if(!segment.mappedMemory){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to map descriptor buffer memory '{}'"), StringConvert(debugName.view()));
        shutdownSegment(segment);
        return false;
    }

    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = segment.buffer;
    segment.deviceAddress = m_context.deviceDispatch.vkGetBufferDeviceAddress(m_context.device, &addressInfo);
    if(segment.deviceAddress == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to query descriptor buffer device address '{}'.")
            , StringConvert(debugName.view())
        );
        shutdownSegment(segment);
        return false;
    }

    segment.capacityBytes = capacityBytes;
    segment.bindingInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
    segment.bindingInfo.address = segment.deviceAddress;
    segment.bindingInfo.usage =
        (&segment == &m_resourceSegment ? VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT : VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT);
    segment.writableOffsetBytes = 0u;

    NWB_MEMSET(segment.mappedMemory, 0, capacityBytes);
    return true;
}

void DescriptorBufferManager::shutdownSegment(SegmentStorage& segment)noexcept{
    static_assert(noexcept(segment.freeRanges.clear()), "descriptor free-range teardown must be non-throwing");
    static_assert(noexcept(segment.liveAllocations.clear()), "descriptor live-allocation teardown must be non-throwing");
    m_allocator.destroyHostMappedBuffer(segment.buffer, segment.allocation, segment.mappedMemory);
    segment.deviceAddress = 0;
    segment.capacityBytes = 0;
    segment.writableOffsetBytes = 0;
    segment.bindingInfo = {};
    segment.freeRanges.clear();
    segment.liveAllocations.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

