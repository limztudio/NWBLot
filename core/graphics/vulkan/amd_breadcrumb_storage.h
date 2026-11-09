// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "amd_breadcrumb_ring.h"
#include "backend_forward.h"
#include "backend_resource_context.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


// AMD breadcrumb ring provides best-effort per-queue observations; physical queues may execute out of sequence.
struct AmdBreadcrumbSlotRecord{
    u64 serial = 0u;
    usize markerHash = 0u;
    u32 marker = 0u;
};
struct AmdBreadcrumbStorage{
    struct Metadata{
        Alloc::PersistentArena arena;
        PersistentUniquePtr<AmdBreadcrumbSlotRecord[]> slotRecords;
        PersistentUniquePtr<u64[]> nextSerials;
        usize slotRecordCount = 0u;
        usize nextSerialCount = 0u;


    public:
        Metadata(const Name& allocationLog, const usize arenaBytes, const usize newSlotRecordCount, const usize newNextSerialCount)
            : arena(allocationLog, arenaBytes)
            , slotRecords(MakePersistentUnique<AmdBreadcrumbSlotRecord[]>(arena, newSlotRecordCount))
            , nextSerials(MakePersistentUnique<u64[]>(arena, newNextSerialCount))
            , slotRecordCount(newSlotRecordCount)
            , nextSerialCount(newNextSerialCount)
        {}
    };

    VkBuffer buffer = VK_NULL_HANDLE;
    VulkanAllocationHandle allocation = {};
    void* mappedMemory = nullptr;
    VulkanDetail::AmdBreadcrumbRing::Layout layout;
    // Prevents concurrent recording from tearing marker records or per-queue reservation serials.
    Futex slotMutex;
    Optional<Metadata> metadata;


public:
    // The caller supplies a validated, non-empty ring layout.
    // Both arrays allocate once with device-lifetime backing, so their spans fit the persistent arena.
    [[nodiscard]] b8 initializeMetadata(const VulkanDetail::AmdBreadcrumbRing::Layout& newLayout, const Name& allocationLog);
    [[nodiscard]] b8 initialize(VulkanAllocator& allocator, const GpuPhysicalQueueTopology& topology);
    void release(VulkanAllocator& allocator)noexcept;
};


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
