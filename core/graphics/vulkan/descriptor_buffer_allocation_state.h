// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


namespace DescriptorBufferSegmentKind{
    static constexpr u8 s_DescriptorBufferSegmentKindNoneBase = 0;
    enum Enum : u8{
        None = s_DescriptorBufferSegmentKindNoneBase,
        Resource,
        Sampler,
    };
};

struct DescriptorBufferSegment{
    // Process-unique identity of the exact resource or sampler storage.
    u64 storageIdentity = 0;
    // Prevents stale handles from writing a recycled byte range.
    u64 allocationSerial = 0;
    u32 offsetBytes = 0;
    u32 sizeBytes = 0;
    DescriptorBufferSegmentKind::Enum kind = DescriptorBufferSegmentKind::None;


public:
    [[nodiscard]] b8 valid()const noexcept{
        return (kind == DescriptorBufferSegmentKind::Resource || kind == DescriptorBufferSegmentKind::Sampler) && sizeBytes > 0 && storageIdentity != 0u && allocationSerial != 0u;
    }
};

class DescriptorBufferAllocationState final : NoCopy{
private:
    struct FreeRange{ u32 offsetBytes = 0u; u32 sizeBytes = 0u; };

public:
    explicit DescriptorBufferAllocationState(Alloc::GlobalArena& arena)noexcept : m_freeRanges(arena), m_liveAllocations(arena){}
    [[nodiscard]] Expected<DescriptorBufferSegment> allocate(DescriptorBufferSegmentKind::Enum kind, u32 sizeBytes, u32 alignmentBytes, u64 storageIdentity, u32 capacityBytes, void* mappedMemory);
    void free(const DescriptorBufferSegment& segment, u64 storageIdentity, u32 capacityBytes);
    [[nodiscard]] b8 isLive(const DescriptorBufferSegment& segment, DescriptorBufferSegmentKind::Enum expectedKind, u64 storageIdentity, u32 capacityBytes)const noexcept;
    [[nodiscard]] b8 ownsAt(const DescriptorBufferSegment& allocation, u32 offsetBytes)const noexcept;
    [[nodiscard]] u32 writableOffsetBytes()const noexcept{ return m_writableOffsetBytes; }
    void resetOffset()noexcept{ m_writableOffsetBytes = 0u; }
    void clearAllocations()noexcept;

private:
    u32 m_writableOffsetBytes = 0u;
    u64 m_nextAllocationSerial = 1u;
    Vector<FreeRange, Alloc::GlobalArena> m_freeRanges;
    Vector<DescriptorBufferSegment, Alloc::GlobalArena> m_liveAllocations;
};


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

