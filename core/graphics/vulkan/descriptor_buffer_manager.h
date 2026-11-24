// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend_resource_context.h"
#include "backend_core_context.h"
#include "descriptor_buffer_allocation_state.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DescriptorBufferManager final : NoCopy{
    friend class Device;
    friend class CommandList;
    friend class GpuDescriptorHeap;
    friend class Queue;


public:
    static constexpr u32 s_ResourceDescriptorBufferIndex = 0u;
    static constexpr u32 s_SamplerDescriptorBufferIndex = 1u;
    static constexpr u32 s_PersistentDescriptorBufferCount = 2u;
    static constexpr u32 s_DescriptorBufferCountWithAccelStruct = 3u;


private:
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
    [[nodiscard]] b8 isEnabledFast()const noexcept{
        return m_fastBindingGeneration.load(MemoryOrder::acquire) != 0u;
    }

    [[nodiscard]] u32 getDescriptorSize(VkDescriptorType descriptorType)const noexcept;
    [[nodiscard]] u32 getImmutableDescriptorSize(VkDescriptorType descriptorType)const noexcept;
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

    [[nodiscard]] Expected<DescriptorBufferSegment> allocate(DescriptorBufferSegmentKind::Enum kind, u32 sizeBytes, u32 alignmentBytes){
        return allocateForBindingGeneration(kind, sizeBytes, alignmentBytes, 0u);
    }

    void free(const DescriptorBufferSegment& segment){
        freeForBindingGeneration(segment, 0u);
    }

    b8 writeDescriptor(const DescriptorWriteItem& item, const DescriptorBufferSegment& allocation, u32 dstOffsetBytes, VkDescriptorType descriptorType);


private:
    [[nodiscard]] Expected<VkResult, VkResult> shutdownForLifecycleOperation();
    void shutdownForDeviceTeardown()noexcept;
    [[nodiscard]] b8 reserveInitialCapacityForBindingGeneration(u32 resourceBytes, u32 samplerBytes, u64 requiredGeneration);
    [[nodiscard]] Expected<DescriptorBufferSegment> allocateForBindingGeneration(DescriptorBufferSegmentKind::Enum kind, u32 sizeBytes, u32 alignmentBytes, u64 requiredGeneration);
    void freeForBindingGeneration(const DescriptorBufferSegment& segment, u64 requiredGeneration);
    [[nodiscard]] Expected<BindingSnapshot> captureBindingSnapshotLocked()const noexcept;
    [[nodiscard]] b8 isLiveSegmentLocked(const SegmentStorage& storage, const DescriptorBufferSegment& segment, DescriptorBufferSegmentKind::Enum expectedKind)const noexcept;
    b8 initializeSegment(SegmentStorage& segment, AStringView debugName, u32 capacityBytes);
    void shutdownSegment(SegmentStorage& segment)noexcept;
    void publishFastBindingGenerationLocked()noexcept{
        m_fastBindingGeneration.store((m_enabled && !m_lifecycleTransitioning) ? m_bindingGeneration : 0u, MemoryOrder::release);
    }


private:
    Device& m_device;
    const VulkanContext& m_context;
    VulkanAllocator& m_allocator;
    Futex m_lifecycleOperationMutex;
    mutable Futex m_lifecycleMutex;
    Atomic<u64> m_fastBindingGeneration{ 0u };
    u64 m_bindingGeneration = 0u;
    u64 m_nextBindingGeneration = 1u;
    b8 m_enabled = false;
    b8 m_lifecycleTransitioning = false;
    b8 m_bufferFormatFeaturesCached = false;
    Array<VkFormatFeatureFlags, Format::kCount> m_cachedBufferFormatFeatures{};
    SegmentStorage m_resourceSegment;
    SegmentStorage m_samplerSegment;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

