// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AmdBreadcrumbRingLayout{
    u16 deviceGeneration = 0u;
    usize physicalQueueCount = 0u;
    usize slotsPerQueue = 0u;
    usize totalSlotCount = 0u;
    VkDeviceSize totalByteSize = 0u;
};

struct AmdBreadcrumbReservation{
    u64 serial = 0u;
    u32 marker = 0u;
    usize localSlot = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool HasBufferDeviceWriteState(ResourceStates::Mask states)noexcept;
[[nodiscard]] VkBufferMemoryBarrier2 BuildHostReadBufferBarrier(VkBuffer buffer)noexcept;
void CollectUniquePhysicalQueueFamilyIndices(
    const GpuPhysicalQueueTopology& topology,
    Vector<u32, Alloc::ScratchArena>& familyIndices
);
[[nodiscard]] bool TryBuildAmdBreadcrumbRingLayout(
    const GpuPhysicalQueueTopology& topology,
    usize slotsPerQueue,
    AmdBreadcrumbRingLayout& layout
)noexcept;
[[nodiscard]] bool TryResolveAmdBreadcrumbRingSlot(
    const AmdBreadcrumbRingLayout& layout,
    const GpuPhysicalQueueId& queue,
    usize localSlot,
    usize& flatSlot,
    VkDeviceSize& byteOffset
)noexcept;
[[nodiscard]] bool TryBuildNextAmdBreadcrumbReservation(
    u64 currentSerial,
    usize slotsPerQueue,
    AmdBreadcrumbReservation& reservation
)noexcept;
[[nodiscard]] bool MatchesAmdBreadcrumbObservation(u32 observedMarker, u32 reservedMarker)noexcept;

#if !defined(NWB_FINAL)
void ResetHostReadbackBarrierAppendCountForTesting()noexcept;
[[nodiscard]] usize GetHostReadbackBarrierAppendCountForTesting()noexcept;
#endif


class HostReadbackBarrierTracker final : NoCopy{
public:
    explicit HostReadbackBarrierTracker(Alloc::GlobalArena& arena);
    ~HostReadbackBarrierTracker() = default;


public:
    [[nodiscard]] bool registerBuffer(VkBuffer buffer);
    void registerDeviceOwnedBuffer(VkBuffer buffer);
    void appendBarriers(Vector<VkBufferMemoryBarrier2, Alloc::GlobalArena>& barriers)const;
    void clear();
    [[nodiscard]] usize size()const{ return m_buffers.size(); }


private:
    Vector<VkBuffer, Alloc::GlobalArena> m_buffers;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

