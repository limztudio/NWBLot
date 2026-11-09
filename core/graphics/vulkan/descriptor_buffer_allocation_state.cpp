// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "descriptor_buffer_allocation_state.h"

#include <core/common/log.h>
#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


Expected<DescriptorBufferSegment> DescriptorBufferAllocationState::allocate(const DescriptorBufferSegmentKind::Enum kind, const u32 sizeBytes, const u32 alignmentBytes, const u64 storageIdentity, const u32 capacityBytes, void* const mappedMemory){
    DescriptorBufferSegment result{};
    auto finishAllocation = [&](const u32 offsetBytes) -> DescriptorBufferSegment{
        result.kind = kind;
        result.offsetBytes = offsetBytes;
        result.sizeBytes = sizeBytes;
        result.storageIdentity = storageIdentity;
        result.allocationSerial = m_nextAllocationSerial++;
        NWB_ASSERT(result.valid() && mappedMemory);
        NWB_MEMSET(static_cast<u8*>(mappedMemory) + result.offsetBytes, 0, result.sizeBytes);
        // Ordered live ranges allow binary-search ownership checks.
        usize insertIndex = 0u;
        while(insertIndex < m_liveAllocations.size() && m_liveAllocations[insertIndex].offsetBytes < result.offsetBytes)
            ++insertIndex;
        m_liveAllocations.insert(m_liveAllocations.begin() + insertIndex, result);
        return result;
    };

    if(m_nextAllocationSerial == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer allocation rejected: allocation serial space is exhausted."));
        return MakeUnexpected(Failure{});
    }

    for(usize i = 0u; i < m_freeRanges.size(); ++i){
        FreeRange range = m_freeRanges[i];
        NWB_ASSERT(range.sizeBytes <= UINT32_MAX - range.offsetBytes);

        const auto alignedOffset = AlignUpChecked<u32>(range.offsetBytes, alignmentBytes);
        if(!alignedOffset)
            continue;

        const u32 rangeEnd = range.offsetBytes + range.sizeBytes;
        if(*alignedOffset >= rangeEnd)
            continue;

        const u32 consumedPrefix = *alignedOffset - range.offsetBytes;
        const u32 remainingBytes = range.sizeBytes - consumedPrefix;
        if(remainingBytes < sizeBytes)
            continue;

        const u32 allocEnd = *alignedOffset + sizeBytes;
        if(consumedPrefix > 0){
            m_freeRanges[i] = { range.offsetBytes, consumedPrefix };
            if(allocEnd < rangeEnd)
                m_freeRanges.insert(m_freeRanges.begin() + i + 1u, { allocEnd, rangeEnd - allocEnd });
        }
        else if(allocEnd < rangeEnd){
            m_freeRanges[i] = { allocEnd, rangeEnd - allocEnd };
        }
        else{
            m_freeRanges.erase(m_freeRanges.begin() + i);
        }

        return finishAllocation(*alignedOffset);
    }

    const auto alignedOffset = AlignUpChecked<u32>(m_writableOffsetBytes, alignmentBytes);
    if(!alignedOffset){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer alignment overflows 32-bit offsets."));
        return MakeUnexpected(Failure{});
    }
    if(*alignedOffset > capacityBytes || sizeBytes > capacityBytes - *alignedOffset){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer is out of space (kind={}, requested={} bytes)."), kind == DescriptorBufferSegmentKind::Sampler ? NWB_TEXT("sampler") : NWB_TEXT("resource"), sizeBytes);
        return MakeUnexpected(Failure{});
    }

    m_writableOffsetBytes = *alignedOffset + sizeBytes;
    return finishAllocation(*alignedOffset);
}

void DescriptorBufferAllocationState::free(const DescriptorBufferSegment& segment, const u64 storageIdentity, const u32 capacityBytes){
    if(segment.storageIdentity != storageIdentity){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer free rejected: allocation belongs to another storage."));
        return;
    }

    if(segment.offsetBytes > capacityBytes || segment.sizeBytes > capacityBytes - segment.offsetBytes || segment.offsetBytes > m_writableOffsetBytes || segment.sizeBytes > m_writableOffsetBytes - segment.offsetBytes){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer free rejected: range {} + {} is outside the live segment."), segment.offsetBytes, segment.sizeBytes);
        return;
    }

    usize allocationIndex = 0u;
    while(allocationIndex < m_liveAllocations.size() && m_liveAllocations[allocationIndex].offsetBytes < segment.offsetBytes)
        ++allocationIndex;
    if(
        allocationIndex == m_liveAllocations.size()
        || m_liveAllocations[allocationIndex].kind != segment.kind
        || m_liveAllocations[allocationIndex].offsetBytes != segment.offsetBytes
        || m_liveAllocations[allocationIndex].sizeBytes != segment.sizeBytes
        || m_liveAllocations[allocationIndex].storageIdentity != segment.storageIdentity
        || m_liveAllocations[allocationIndex].allocationSerial != segment.allocationSerial
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer free rejected: range {} + {} is not a live allocation."), segment.offsetBytes, segment.sizeBytes);
        return;
    }

    // Free ranges stay inside the u32 segment capacity.
    const auto rangeEnd = [](const FreeRange& range)noexcept -> u32{
        NWB_ASSERT(range.sizeBytes <= UINT32_MAX - range.offsetBytes);
        return range.offsetBytes + range.sizeBytes;
    };

    FreeRange freedRange{ segment.offsetBytes, segment.sizeBytes };
    for(const FreeRange& range : m_freeRanges){
        if(freedRange.offsetBytes < rangeEnd(range) && range.offsetBytes < freedRange.offsetBytes + freedRange.sizeBytes){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Descriptor buffer free rejected: range {} + {} overlaps a free range."), segment.offsetBytes, segment.sizeBytes);
            return;
        }
    }

    m_liveAllocations.erase(m_liveAllocations.begin() + allocationIndex);
    usize insertIndex = 0u;
    while(insertIndex < m_freeRanges.size() && m_freeRanges[insertIndex].offsetBytes < freedRange.offsetBytes)
        ++insertIndex;

    m_freeRanges.insert(m_freeRanges.begin() + insertIndex, freedRange);

    const auto mergeAdjacentAt = [&](const usize leftIndex)noexcept -> b8{
        if(leftIndex + 1u >= m_freeRanges.size())
            return false;

        FreeRange& left = m_freeRanges[leftIndex];
        const FreeRange right = m_freeRanges[leftIndex + 1u];

        if(rangeEnd(left) != right.offsetBytes)
            return false;

        NWB_ASSERT(right.sizeBytes <= UINT32_MAX - left.sizeBytes);
        left.sizeBytes += right.sizeBytes;
        m_freeRanges.erase(m_freeRanges.begin() + leftIndex + 1u);
        return true;
    };

    if(mergeAdjacentAt(insertIndex))
        mergeAdjacentAt(insertIndex);
    if(insertIndex > 0)
        mergeAdjacentAt(insertIndex - 1u);
}

b8 DescriptorBufferAllocationState::isLive(const DescriptorBufferSegment& segment, const DescriptorBufferSegmentKind::Enum expectedKind, const u64 storageIdentity, const u32 capacityBytes)const noexcept{
    if(
        !segment.valid()
        || segment.kind != expectedKind
        || segment.storageIdentity != storageIdentity
        || segment.offsetBytes > capacityBytes
        || segment.sizeBytes > capacityBytes - segment.offsetBytes
        || segment.offsetBytes > m_writableOffsetBytes
        || segment.sizeBytes > m_writableOffsetBytes - segment.offsetBytes
    )
        return false;

    const auto found = LowerBound(m_liveAllocations.begin(), m_liveAllocations.end(), segment.offsetBytes, [](const DescriptorBufferSegment& item, const u32 value)noexcept{
        return item.offsetBytes < value;
    });
    if(found == m_liveAllocations.end())
        return false;

    const DescriptorBufferSegment& liveSegment = *found;
    return liveSegment.kind == segment.kind && liveSegment.offsetBytes == segment.offsetBytes && liveSegment.sizeBytes == segment.sizeBytes && liveSegment.storageIdentity == segment.storageIdentity && liveSegment.allocationSerial == segment.allocationSerial;
}

b8 DescriptorBufferAllocationState::ownsAt(const DescriptorBufferSegment& allocation, const u32 dstOffsetBytes)const noexcept{
    const auto firstAfter = UpperBound(m_liveAllocations.begin(), m_liveAllocations.end(), dstOffsetBytes, [](const u32 value, const DescriptorBufferSegment& item)noexcept{
        return value < item.offsetBytes;
    });

    b8 ownsLiveAllocation = false;
    if(firstAfter != m_liveAllocations.begin()){
        const DescriptorBufferSegment& liveAllocation = *(firstAfter - 1u);
        ownsLiveAllocation = liveAllocation.kind == allocation.kind
            && liveAllocation.offsetBytes == allocation.offsetBytes
            && liveAllocation.sizeBytes == allocation.sizeBytes
            && liveAllocation.storageIdentity == allocation.storageIdentity
            && liveAllocation.allocationSerial == allocation.allocationSerial
        ;
    }
    return ownsLiveAllocation;
}

void DescriptorBufferAllocationState::clearAllocations()noexcept{
    static_assert(noexcept(m_freeRanges.clear()));
    static_assert(noexcept(m_liveAllocations.clear()));
    m_freeRanges.clear();
    m_liveAllocations.clear();
}


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

