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
    usize physicalQueueCount = 0u;
    usize slotsPerQueue = 0u;
    usize totalSlotCount = 0u;
    VkDeviceSize totalByteSize = 0u;
    u16 deviceGeneration = 0u;
};

struct AmdBreadcrumbRingSlot{
    usize flatSlot = 0u;
    VkDeviceSize byteOffset = 0u;
};

struct AmdBreadcrumbReservation{
    u64 serial = 0u;
    usize localSlot = 0u;
    u32 marker = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool HasBufferDeviceWriteState(ResourceStates::Mask states)noexcept;
[[nodiscard]] VkBufferMemoryBarrier2 BuildHostReadBufferBarrier(VkBuffer buffer)noexcept;
void CollectUniquePhysicalQueueFamilyIndices(
    const GpuPhysicalQueueTopology& topology,
    Vector<u32, Alloc::ScratchArena>& familyIndices
);
[[nodiscard]] Expected<AmdBreadcrumbRingLayout> TryBuildAmdBreadcrumbRingLayout(
    const GpuPhysicalQueueTopology& topology,
    usize slotsPerQueue
)noexcept;
[[nodiscard]] Expected<AmdBreadcrumbRingSlot> TryResolveAmdBreadcrumbRingSlot(
    const AmdBreadcrumbRingLayout& layout,
    const GpuPhysicalQueueId& queue,
    usize localSlot
)noexcept;
[[nodiscard]] Expected<AmdBreadcrumbReservation> TryBuildNextAmdBreadcrumbReservation(
    u64 currentSerial,
    usize slotsPerQueue
)noexcept;
[[nodiscard]] bool MatchesAmdBreadcrumbObservation(u32 observedMarker, u32 reservedMarker)noexcept;

class HostReadbackBarrierTracker final : NoCopy{
public:
    explicit HostReadbackBarrierTracker(Alloc::GlobalArena& arena);
    ~HostReadbackBarrierTracker() = default;


public:
    [[nodiscard]] bool registerBuffer(VkBuffer buffer);
    void registerDeviceOwnedBuffer(VkBuffer buffer);
    void appendBarriers(Vector<VkBufferMemoryBarrier2, Alloc::GlobalArena>& barriers)const;
    void clear()noexcept;
    [[nodiscard]] usize size()const noexcept{ return m_buffers.size(); }


private:
    Vector<VkBuffer, Alloc::GlobalArena> m_buffers;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

