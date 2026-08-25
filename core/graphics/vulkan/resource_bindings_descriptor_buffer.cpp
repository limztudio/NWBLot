// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "resource_bindings_detail.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Futex s_DescriptorBufferStorageIdentityMutex;
static u64 s_NextDescriptorBufferStorageIdentity = 1u;

[[nodiscard]] static u64 AllocateDescriptorBufferStorageIdentity(){
    ScopedLock lock(s_DescriptorBufferStorageIdentityMutex);

    if(s_NextDescriptorBufferStorageIdentity == 0u)
        return 0u;

    const u64 identity = s_NextDescriptorBufferStorageIdentity;
    s_NextDescriptorBufferStorageIdentity = identity == UINT64_MAX ? 0u : identity + 1u;
    return identity;
}

[[nodiscard]] static constexpr bool UsesDescriptorBufferInfo(const ResourceType::Enum type){
    switch(type){
    case ResourceType::ConstantBuffer:
    case ResourceType::StructuredBuffer_SRV:
    case ResourceType::StructuredBuffer_UAV:
    case ResourceType::RawBuffer_SRV:
    case ResourceType::RawBuffer_UAV:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] static bool ResolveDescriptorBufferRange(
    const DescriptorWriteItem& item,
    const BufferDesc& bufferDesc,
    BufferRange& outRange
){
    outRange = item.range.resolve(bufferDesc);
    return outRange.byteSize > 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DescriptorBufferManager::DescriptorBufferManager(Device& device, const VulkanContext& context, VulkanAllocator& allocator)
    : m_device(device)
    , m_context(context)
    , m_allocator(allocator)
    , m_resourceSegment(context.objectArena, VulkanDetail::AllocateDescriptorBufferStorageIdentity())
    , m_samplerSegment(context.objectArena, VulkanDetail::AllocateDescriptorBufferStorageIdentity())
{}
DescriptorBufferManager::~DescriptorBufferManager(){
    shutdown();
}

bool DescriptorBufferManager::initialize(){
    shutdown();

    if(!m_context.extensions.EXT_descriptor_buffer)
        return false;
    if(m_resourceSegment.storageIdentity == 0u || m_samplerSegment.storageIdentity == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor-buffer storage identity space is exhausted."));
        return false;
    }

    const auto& props = m_context.descriptorBufferProperties;

    // Cap persistent mapped segment reservations at practical working sizes.
    constexpr u32 s_TargetResourceSegmentBytes = 32u * 1024u * 1024u;
    constexpr u32 s_TargetSamplerSegmentBytes = 2u * 1024u * 1024u;

    if(props.descriptorBufferOffsetAlignment == 0 || props.descriptorBufferOffsetAlignment > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer offset alignment is outside the supported 32-bit range."));
        return false;
    }

    const u32 offsetAlignment = static_cast<u32>(props.descriptorBufferOffsetAlignment);
    if(
        props.maxDescriptorBufferBindings < DescriptorBufferManager::s_PersistentDescriptorBufferCount
        || props.maxResourceDescriptorBufferBindings == 0u
        || props.maxSamplerDescriptorBufferBindings == 0u
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer limits cannot bind the required resource and sampler segments."));
        return false;
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
        return false;
    }

    const VkDeviceSize totalCapacityBytes = static_cast<VkDeviceSize>(resourceCapacityBytes) + samplerCapacityBytes;
    if(totalCapacityBytes > props.descriptorBufferAddressSpaceSize){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer global address space {} cannot hold the requested {} bytes of resource and sampler segments.")
            , props.descriptorBufferAddressSpaceSize
            , totalCapacityBytes
        );
        return false;
    }

    if(!initializeSegment(m_resourceSegment, "vk_resource_descriptor_buffer", resourceCapacityBytes)){
        shutdown();
        return false;
    }

    if(!initializeSegment(m_samplerSegment, "vk_sampler_descriptor_buffer", samplerCapacityBytes)){
        shutdown();
        return false;
    }

    m_enabled = true;
    return true;
}

void DescriptorBufferManager::shutdown(){
    shutdownSegment(m_resourceSegment);
    shutdownSegment(m_samplerSegment);
    m_enabled = false;
}

u32 DescriptorBufferManager::getDescriptorSize(const VkDescriptorType descriptorType)const{
    if(!m_enabled)
        return 0;

    const auto& props = m_context.descriptorBufferProperties;
    VkDeviceSize size = 0;
    switch(descriptorType){
    case VK_DESCRIPTOR_TYPE_SAMPLER:                               size = props.samplerDescriptorSize; break;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:                size = props.combinedImageSamplerDescriptorSize; break;
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:                         size = props.sampledImageDescriptorSize; break;
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:                         size = props.storageImageDescriptorSize; break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:                  size = props.uniformTexelBufferDescriptorSize; break;
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:                  size = props.storageTexelBufferDescriptorSize; break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:                        size = props.uniformBufferDescriptorSize; break;
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:                        size = props.storageBufferDescriptorSize; break;
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:                      size = props.inputAttachmentDescriptorSize; break;
    case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:            size = props.accelerationStructureDescriptorSize; break;
    default:                                                       return 0;
    }

    return size > UINT32_MAX ? 0u : static_cast<u32>(size);
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

DescriptorBufferSegment DescriptorBufferManager::allocate(const DescriptorBufferSegmentKind::Enum kind, const u32 sizeBytes, const u32 alignmentBytes){
    DescriptorBufferSegment result{};
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

bool DescriptorBufferManager::writeDescriptor(
    const DescriptorWriteItem& item,
    const DescriptorBufferSegment& allocation,
    const u32 dstOffsetBytes,
    const VkDescriptorType descriptorType
){
    if(!m_enabled)
        return false;
    if(!allocation.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: allocation is invalid."));
        return false;
    }
    if(item.type == ResourceType::VolatileConstantBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Persistent descriptor-buffer writes reject volatile constant buffers."));
        return false;
    }
    if(
        !VulkanDetail::IsSupportedDescriptorBindingType(item.type)
        || VulkanDetail::ConvertDescriptorType(item.type) != descriptorType
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: resource type {} does not match descriptor type {}.")
            , static_cast<u32>(item.type)
            , static_cast<u32>(descriptorType)
        );
        return false;
    }

    const bool isSampler = (descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER);
    const DescriptorBufferSegmentKind::Enum expectedKind = isSampler
        ? DescriptorBufferSegmentKind::Sampler
        : DescriptorBufferSegmentKind::Resource
    ;
    if(allocation.kind != expectedKind){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: allocation has the wrong segment kind."));
        return false;
    }
    SegmentStorage& storage = isSampler ? m_samplerSegment : m_resourceSegment;
    if(allocation.storageIdentity != storage.storageIdentity){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: allocation belongs to another storage."));
        return false;
    }

    const u32 descriptorSize = getDescriptorSize(descriptorType);
    if(descriptorSize == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: unknown size for descriptor type {}."), static_cast<u32>(descriptorType));
        return false;
    }
    if(!item.resourceHandle){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: resource handle is null."));
        return false;
    }
    if(dstOffsetBytes < allocation.offsetBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: offset {} precedes its allocation."), dstOffsetBytes);
        return false;
    }
    const u32 allocationRelativeOffsetBytes = dstOffsetBytes - allocation.offsetBytes;
    if(
        allocationRelativeOffsetBytes > allocation.sizeBytes
        || descriptorSize > allocation.sizeBytes - allocationRelativeOffsetBytes
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: offset {} + size {} exceeds its allocation."), dstOffsetBytes, descriptorSize);
        return false;
    }

    ScopedLock lock(storage.mutex);

    auto* dstBytes = static_cast<u8*>(storage.mappedMemory);
    if(!dstBytes)
        return false;
    if(dstOffsetBytes > storage.capacityBytes || descriptorSize > storage.capacityBytes - dstOffsetBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: offset {} + size {} exceeds segment capacity {}.")
            , dstOffsetBytes, descriptorSize, storage.capacityBytes
        );
        return false;
    }
    // Validate full live-allocation identity to reject stale recycled ranges.
    usize first = 0u;
    usize last = storage.liveAllocations.size();
    while(first < last){
        const usize middle = first + (last - first) / 2u;
        if(storage.liveAllocations[middle].offsetBytes <= dstOffsetBytes)
            first = middle + 1u;
        else
            last = middle;
    }

    bool ownsLiveAllocation = false;
    if(first > 0u){
        const DescriptorBufferSegment& liveAllocation = storage.liveAllocations[first - 1u];
        ownsLiveAllocation = liveAllocation.kind == allocation.kind
            && liveAllocation.offsetBytes == allocation.offsetBytes
            && liveAllocation.sizeBytes == allocation.sizeBytes
            && liveAllocation.storageIdentity == allocation.storageIdentity
            && liveAllocation.allocationSerial == allocation.allocationSerial
        ;
    }
    if(!ownsLiveAllocation){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: allocation is not live for offset {}."), dstOffsetBytes);
        return false;
    }

    auto getInfo = VulkanDetail::MakeVkStruct<VkDescriptorGetInfoEXT>(VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT);
    getInfo.type = descriptorType;

    VkDescriptorAddressInfoEXT addressInfo{};
    VkDescriptorImageInfo imageInfo{};
    VkSampler samplerHandle = VK_NULL_HANDLE;
    VkDeviceAddress accelStructAddress = 0;

    if(VulkanDetail::UsesDescriptorBufferInfo(item.type)){
        auto* buffer = checked_cast<Buffer*>(item.resourceHandle);
        if(!m_device.isBufferReadyForGpuUse(buffer)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected a foreign or unready Buffer."));
            return false;
        }
        const BufferDesc& bufferDesc = buffer->m_creationDesc;
        const bool isUniform = item.type == ResourceType::ConstantBuffer;
        const VkBufferUsageFlags requiredUsage = isUniform
            ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
            : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        ;
        if((buffer->m_usage & requiredUsage) == 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: Buffer lacks native descriptor usage."));
            return false;
        }
        if(isUniform && (!bufferDesc.isConstantBuffer || bufferDesc.isVolatile)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: Buffer is not a persistent constant buffer."));
            return false;
        }
        if(
            (item.type == ResourceType::RawBuffer_SRV || item.type == ResourceType::RawBuffer_UAV)
            && !bufferDesc.canHaveRawViews
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: Buffer has no raw-view capability."));
            return false;
        }
        if(
            (item.type == ResourceType::StructuredBuffer_SRV || item.type == ResourceType::StructuredBuffer_UAV)
            && bufferDesc.structStride == 0u
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: Buffer is not structured."));
            return false;
        }
        if(
            (item.type == ResourceType::StructuredBuffer_UAV || item.type == ResourceType::RawBuffer_UAV)
            && !bufferDesc.canHaveUAVs
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: Buffer has no UAV capability."));
            return false;
        }
        BufferRange range;
        if(!VulkanDetail::ResolveDescriptorBufferRange(item, bufferDesc, range))
            return false;
        const VkDeviceAddress bufferAddress = static_cast<VkDeviceAddress>(buffer->getGpuVirtualAddress());
        if(bufferAddress == 0u || range.byteOffset > UINT64_MAX - bufferAddress){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: buffer has no valid device address."));
            return false;
        }
        const VkDeviceAddress descriptorAddress = bufferAddress + range.byteOffset;
        const u64 requiredAlignment = isUniform
            ? getUniformBufferAddressAlignmentBytes()
            : getStorageBufferAddressAlignmentBytes()
        ;
        if(
            (descriptorAddress % requiredAlignment) != 0u
            || range.byteSize > UINT64_MAX - descriptorAddress
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: buffer address or range is invalid."));
            return false;
        }
        addressInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
        addressInfo.address = descriptorAddress;
        addressInfo.range = range.byteSize;
        if(isUniform)
            getInfo.data.pUniformBuffer = &addressInfo;
        else
            getInfo.data.pStorageBuffer = &addressInfo;
    }
    else{
        switch(item.type){
        case ResourceType::TypedBuffer_SRV:
        case ResourceType::TypedBuffer_UAV:{
            auto* buffer = checked_cast<Buffer*>(item.resourceHandle);
            if(!m_device.isBufferReadyForGpuUse(buffer)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected a foreign or unready typed Buffer."));
                return false;
            }
            const BufferDesc& bufferDesc = buffer->m_creationDesc;
            const VkBufferUsageFlags requiredUsage = item.type == ResourceType::TypedBuffer_UAV
                ? VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT
                : VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT
            ;
            if(
                !bufferDesc.canHaveTypedViews
                || (item.type == ResourceType::TypedBuffer_UAV && !bufferDesc.canHaveUAVs)
                || (buffer->m_usage & requiredUsage) == 0u
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: Buffer lacks typed-view capability."));
                return false;
            }
            BufferRange range;
            if(!VulkanDetail::ResolveDescriptorBufferRange(item, bufferDesc, range))
                return false;
            const VkDeviceAddress bufferAddress = static_cast<VkDeviceAddress>(buffer->getGpuVirtualAddress());
            if(bufferAddress == 0u || range.byteOffset > UINT64_MAX - bufferAddress){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: typed buffer has no valid device address."));
                return false;
            }
            const Format::Enum viewFormat = item.format != Format::UNKNOWN ? item.format : bufferDesc.format;
            if(viewFormat == Format::UNKNOWN || viewFormat >= Format::kCount){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: typed-buffer format is invalid."));
                return false;
            }
            const VkFormat vkFormat = ConvertFormat(viewFormat);
            const FormatInfo& formatInfo = GetFormatInfo(viewFormat);
            if(vkFormat == VK_FORMAT_UNDEFINED || formatInfo.bytesPerBlock == 0u){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: typed-buffer format is unsupported."));
                return false;
            }
            VkFormatProperties formatProperties{};
            vkGetPhysicalDeviceFormatProperties(m_context.physicalDevice, vkFormat, &formatProperties);
            const VkFormatFeatureFlags requiredFormatFeature = item.type == ResourceType::TypedBuffer_UAV
                ? VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT
                : VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT
            ;
            if((formatProperties.bufferFeatures & requiredFormatFeature) == 0u){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: format lacks texel-buffer support."));
                return false;
            }
            const VkDeviceAddress descriptorAddress = bufferAddress + range.byteOffset;
            if(
                (descriptorAddress % getTexelBufferAddressAlignmentBytes()) != 0u
                || (range.byteOffset % formatInfo.bytesPerBlock) != 0u
                || (range.byteSize % formatInfo.bytesPerBlock) != 0u
                || (range.byteSize / formatInfo.bytesPerBlock) > getMaxTexelBufferElements()
                || range.byteSize > UINT64_MAX - descriptorAddress
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: typed-buffer range is invalid."));
                return false;
            }
            addressInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
            addressInfo.address = descriptorAddress;
            addressInfo.range = range.byteSize;
            addressInfo.format = vkFormat;
            if(item.type == ResourceType::TypedBuffer_UAV)
                getInfo.data.pStorageTexelBuffer = &addressInfo;
            else
                getInfo.data.pUniformTexelBuffer = &addressInfo;
            break;
        }
        case ResourceType::Texture_SRV:
        case ResourceType::Texture_UAV:{
            auto* texture = checked_cast<Texture*>(item.resourceHandle);
            if(!m_device.isTextureReadyForGpuUse(texture)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected a foreign or unready Texture."));
                return false;
            }
            const VkImageUsageFlags requiredUsage = item.type == ResourceType::Texture_UAV
                ? VK_IMAGE_USAGE_STORAGE_BIT
                : VK_IMAGE_USAGE_SAMPLED_BIT
            ;
            if((texture->m_imageInfo.usage & requiredUsage) == 0u){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected: Texture lacks native descriptor usage."));
                return false;
            }
            imageInfo.imageView = texture->getView(item.subresources, item.dimension, item.format);
            if(imageInfo.imageView == VK_NULL_HANDLE)
                return false;
            imageInfo.imageLayout = item.type == ResourceType::Texture_UAV ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            getInfo.data.pSampledImage = &imageInfo;
            getInfo.data.pStorageImage = &imageInfo;
            break;
        }
        case ResourceType::Sampler:{
            auto* sampler = checked_cast<Sampler*>(item.resourceHandle);
            if(&sampler->m_context != &m_context){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected a foreign Sampler."));
                return false;
            }
            samplerHandle = sampler->m_sampler;
            if(samplerHandle == VK_NULL_HANDLE){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected an unready Sampler."));
                return false;
            }
            getInfo.data.pSampler = &samplerHandle;
            break;
        }
        case ResourceType::RayTracingAccelStruct:{
            // TLAS descriptor directly encodes the generation's device address.
            auto* as = checked_cast<AccelStruct*>(item.resourceHandle);
            if(!m_device.isAccelStructReadyForGpuUse(as)){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected a foreign or unready AccelStruct."));
                return false;
            }
            if(!as->m_isTopLevelAtCreation){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer write rejected a bottom-level AccelStruct."));
                return false;
            }
            accelStructAddress = static_cast<VkDeviceAddress>(as->getDeviceAddress());
            if(accelStructAddress == 0u)
                return false;
            getInfo.data.accelerationStructure = accelStructAddress;
            break;
        }
        default:
            return false;
        }
    }

    vkGetDescriptorEXT(m_context.device, &getInfo, descriptorSize, dstBytes + dstOffsetBytes);
    return true;
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
    segment.deviceAddress = vkGetBufferDeviceAddress(m_context.device, &addressInfo);
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

void DescriptorBufferManager::shutdownSegment(SegmentStorage& segment){
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

