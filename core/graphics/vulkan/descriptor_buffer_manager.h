// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend_resource_context.h"
#include "backend_core_context.h"
#include "descriptor_buffer_allocation_state.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Descriptor-buffer manager: host-mapped resource/sampler segments bind by byte offset.

class DescriptorBufferManager final : NoCopy{
    friend class Device;
    friend class CommandList;
    friend class GpuDescriptorHeap;
    friend class Queue;


public:
    // Resource and sampler descriptor buffers are always bound in this order.
    // A TLAS set reuses the resource descriptor buffer, so the optional third offset extends this fixed topology.
    static constexpr u32 s_ResourceDescriptorBufferIndex = 0u;
    static constexpr u32 s_SamplerDescriptorBufferIndex = 1u;
    static constexpr u32 s_PersistentDescriptorBufferCount = 2u;
    static constexpr u32 s_DescriptorBufferCountWithAccelStruct = 3u;


private:
    // Host-mapped descriptor segment with synchronized free ranges and live-allocation tracking.
    struct SegmentStorage{
        const Expected<u64> storageIdentity;
        VkBuffer buffer = VK_NULL_HANDLE;
        VulkanAllocationHandle allocation = nullptr;
        void* mappedMemory = nullptr;
        VkDeviceAddress deviceAddress = 0;
        u32 capacityBytes = 0;
        VkDescriptorBufferBindingInfoEXT bindingInfo{};
        Futex mutex;
        DescriptorBufferAllocationState allocations;


    public:
        SegmentStorage(Alloc::GlobalArena& arena, Expected<u64> storageIdentityValue)noexcept
            : storageIdentity(Move(storageIdentityValue))
            , allocations(arena)
        {}
    };
    struct BindingSnapshot{
        Array<VkDescriptorBufferBindingInfoEXT, s_PersistentDescriptorBufferCount> bindingInfos{};
        u64 generation = 0u;
        u32 offsetAlignmentBytes = 0u;
    };


public:
    DescriptorBufferManager(Device& device, const VulkanContext& context, VulkanAllocator& allocator)noexcept;
    ~DescriptorBufferManager()noexcept{
        shutdownForDeviceTeardown();
    }


public:
    b8 initialize();
    [[nodiscard]] b8 shutdown();

    [[nodiscard]] b8 isEnabled()const noexcept;

    // Exact driver descriptor size; 0 when descriptor buffers are disabled.
    [[nodiscard]] u32 getDescriptorSize(VkDescriptorType descriptorType)const noexcept;
    [[nodiscard]] u32 getOffsetAlignmentBytes()const noexcept;
    [[nodiscard]] u64 getUniformBufferAddressAlignmentBytes()const noexcept{
        return Max<u64>(m_context.physicalDeviceProperties.limits.minUniformBufferOffsetAlignment, 1u);
    }

    [[nodiscard]] u64 getStorageBufferAddressAlignmentBytes()const noexcept{
        return Max<u64>(m_context.physicalDeviceProperties.limits.minStorageBufferOffsetAlignment, 1u);
    }

    [[nodiscard]] u64 getTexelBufferAddressAlignmentBytes()const noexcept{
        return Max<u64>(m_context.physicalDeviceProperties.limits.minTexelBufferOffsetAlignment, 1u);
    }

    [[nodiscard]] u32 getMaxTexelBufferElements()const noexcept{
        return m_context.physicalDeviceProperties.limits.maxTexelBufferElements;
    }

    // Allocates aligned, zeroed descriptor bytes from free ranges or the bump pointer.
    [[nodiscard]] Expected<DescriptorBufferSegment> allocate(DescriptorBufferSegmentKind::Enum kind, u32 sizeBytes, u32 alignmentBytes){
        return allocateForBindingGeneration(kind, sizeBytes, alignmentBytes, 0u);
    }

    void free(const DescriptorBufferSegment& segment){
        freeForBindingGeneration(segment, 0u);
    }

    // Validates allocation identity, then encodes one descriptor.
    b8 writeDescriptor(const DescriptorWriteItem& item, const DescriptorBufferSegment& allocation, u32 dstOffsetBytes, VkDescriptorType descriptorType);


private:
    [[nodiscard]] Expected<VkResult, VkResult> shutdownForLifecycleOperation();
    void shutdownForDeviceTeardown()noexcept;
    // Driver layout sizes may exceed the working-size defaults; addresses become immutable after the first allocation in a generation.
    [[nodiscard]] b8 reserveInitialCapacityForBindingGeneration(u32 resourceBytes, u32 samplerBytes, u64 requiredGeneration);
    [[nodiscard]] Expected<DescriptorBufferSegment> allocateForBindingGeneration(DescriptorBufferSegmentKind::Enum kind, u32 sizeBytes, u32 alignmentBytes, u64 requiredGeneration);
    void freeForBindingGeneration(const DescriptorBufferSegment& segment, u64 requiredGeneration);
    [[nodiscard]] Expected<BindingSnapshot> captureBindingSnapshotLocked()const noexcept;
    [[nodiscard]] b8 isLiveSegmentLocked(const SegmentStorage& storage, const DescriptorBufferSegment& segment, DescriptorBufferSegmentKind::Enum expectedKind)const noexcept;
    b8 initializeSegment(SegmentStorage& segment, AStringView debugName, u32 capacityBytes);
    void shutdownSegment(SegmentStorage& segment)noexcept;


private:
    Device& m_device;
    const VulkanContext& m_context;
    VulkanAllocator& m_allocator;
    Futex m_lifecycleOperationMutex;
    mutable Futex m_lifecycleMutex;
    u64 m_bindingGeneration = 0u;
    u64 m_nextBindingGeneration = 1u;
    b8 m_enabled = false;
    b8 m_lifecycleTransitioning = false;
    SegmentStorage m_resourceSegment;
    SegmentStorage m_samplerSegment;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

