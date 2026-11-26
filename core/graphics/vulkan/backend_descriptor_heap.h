// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"
#include "backend_forward.h"
#include "backend_core_context.h"
#include "descriptor_buffer_manager.h"

#include <core/task/cpu/scheduler.h>

#include "command_buffer_resource_references.h"
#include "heap_binding_contract.h"
#include "host_readback_sync.h"
#include "native_resource_provenance.h"
#include "native_queue_state.h"
#include "submitted_command_buffer_owner_lookup.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuDescriptorHeap final : NoCopy{
    friend class Device;
    friend class CommandList;
    friend class Queue;
    friend class TrackedCommandBuffer;


private:
    class DescriptorBufferWriter;

    template<typename T>
    class FixedTable final : NoCopy{
    public:
        [[nodiscard]] static usize RequiredBytes(usize count){
            return count == 0u ? 0u : Alloc::PersistentArena::StructureAlignedSize(SizeOf<sizeof(T)>(count), alignof(T));
        }


    public:
        [[nodiscard]] T& operator[](usize index)noexcept{ return m_storage[index]; }

        [[nodiscard]] const T& operator[](usize index)const noexcept{ return m_storage[index]; }


    public:
        [[nodiscard]] b8 initialize(Alloc::PersistentArena& arena, usize count){
            clear();
            if(count == 0u)
                return true;
            m_storage = MakePersistentUnique<T[]>(arena, count);
            if(!m_storage)
                return false;
            m_count = count;
            return true;
        }

        void clear()noexcept{
            m_storage.reset();
            m_count = 0u;
        }

        [[nodiscard]] usize size()const noexcept{ return m_count; }

        [[nodiscard]] T* begin()noexcept{ return m_storage.get(); }

        [[nodiscard]] const T* begin()const noexcept{ return m_storage.get(); }

        [[nodiscard]] T* end()noexcept{ return m_count == 0u ? begin() : begin() + m_count; }

        [[nodiscard]] const T* end()const noexcept{ return m_count == 0u ? begin() : begin() + m_count; }


    private:
        PersistentUniquePtr<T[]> m_storage;
        usize m_count = 0u;
    };

    struct SlotState{
        enum Enum : u8{
            Free,
            Live,
            PendingRecording,
            Retired,
        };
    };

    struct SlotAllocator{
        u32 capacity = 0;
        u32 nextFresh = 0;
        FixedTable<u32> freeList;
        usize freeCount = 0u;
        FixedTable<SlotState::Enum> slotStates;
        FixedTable<u8> allocatedClasses;


    public:
        [[nodiscard]] static usize RequiredBytes(u32 capacity);


    public:
        [[nodiscard]] b8 initialize(Alloc::PersistentArena& arena, u32 newCapacity);
        void clear()noexcept;
    };
    struct RetiredSlot{
        u64 lastRequiredHeapUseID = 0u;
        GpuDescriptorHandle handle;
    };
    static_assert(IsTriviallyCopyable_V<RetiredSlot>, "descriptor retirement publication must remain a scalar journal write");
    struct HeapUse{
        TrackedCommandBuffer* commandBuffer = nullptr;
        QueueSubmissionToken submissionToken;
        u64 id = 0u;
        GpuPhysicalQueueId physicalQueue;
    };


public:
    class PendingRecordingLease final : NoCopy{
        friend class GpuDescriptorHeap;


    private:
        explicit PendingRecordingLease(GpuDescriptorHeap& heap);
    public:
        ~PendingRecordingLease()noexcept;


    public:
        [[nodiscard]] b8 valid()const noexcept{ return m_heap != nullptr; }


    private:
        GpuDescriptorHeap* m_heap = nullptr;
        u64 m_descriptorBufferGeneration = 0u;
    };


public:
    explicit GpuDescriptorHeap(Device& device)noexcept;
    ~GpuDescriptorHeap()noexcept;


public:
    [[nodiscard]] PendingRecordingLease acquirePendingRecordingLease();

    b8 initialize(const GpuDescriptorHeapDesc& desc);
    void shutdown();

    [[nodiscard]] b8 isInitialized()const noexcept{ return m_initialized.load(MemoryOrder::acquire); }
    [[nodiscard]] GpuDescriptorHeapLifecycleStatistics lifecycleStatistics()const noexcept;

    [[nodiscard]] Expected<GpuDescriptorHandle> allocate(GpuDescriptorClass::Enum descriptorClass);

    void free(GpuDescriptorHandle handle);

    void collectRetired();

    b8 write(GpuDescriptorHandle handle, const DescriptorWriteItem& item);

    void bindCompute(CommandList& commandList, const ComputePipeline& pipeline, GpuDescriptorHandle accelStructHandle = GpuDescriptorHandle::Invalid());

    void bindGraphics(CommandList& commandList, const GraphicsPipeline& pipeline);
    void bindGraphics(CommandList& commandList, const MeshletPipeline& pipeline);

    void bindRayTracing(CommandList& commandList, const RayTracingPipeline& pipeline, GpuDescriptorHandle accelStructHandle = GpuDescriptorHandle::Invalid());

    [[nodiscard]] const BindingLayoutHandle& getResourceLayout()const noexcept{ return m_resourceLayout; }

    [[nodiscard]] const BindingLayoutHandle& getSamplerLayout()const noexcept{ return m_samplerLayout; }
    [[nodiscard]] const BindingLayoutHandle& getAccelStructLayout()const noexcept{ return m_accelStructLayout; }

    [[nodiscard]] b8 hasAccelStructLayout()const noexcept{ return m_accelStructLayout != nullptr; }
    [[nodiscard]] u32 getRegisterSlot(GpuDescriptorClass::Enum descriptorClass)const noexcept;


private:
    [[nodiscard]] SlotAllocator& allocatorForClass(GpuDescriptorClass::Enum descriptorClass)noexcept;
    template<typename HandleT, typename ResourceT>
    [[nodiscard]] b8 commitDescriptorWriteLocked(
        ResourceT* resource,
        GpuDescriptorHandle handle,
        const DescriptorWriteItem& writeItem,
        GpuDescriptorClass::Enum descriptorClass,
        b8 needsDynamicReadiness,
        b8 needsConcurrentAdmission,
        b8 alwaysPublishFastVersion,
        HandleT& retained
    );
    void releaseAccelStructDescriptorBlock(u32 slot);
    void releaseRetainedDescriptorResource(GpuDescriptorHandle handle);
    [[nodiscard]] b8 isResourceAdmittedToActiveUsesLocked(const ResourceQueueAdmissionSnapshot& admission)const noexcept;
    [[nodiscard]] b8 retainedResourcesReadyForQueueLocked(const GpuPhysicalQueueInfo& queue)const noexcept;
    [[nodiscard]] b8 retainedResourcesReadyForQueue(const GpuPhysicalQueueId& queue)const noexcept;
    [[nodiscard]] b8 trackCommandBufferUseLocked(TrackedCommandBuffer& commandBuffer, const GpuPhysicalQueueId& physicalQueue);
    [[nodiscard]] Expected<usize> validateCommandBufferUseSubmissionLocked(TrackedCommandBuffer& commandBuffer, const QueueSubmissionToken& submissionToken);
    void commitCommandBufferUseSubmissionLocked(TrackedCommandBuffer& commandBuffer, const QueueSubmissionToken& submissionToken, usize heapUseIndex)noexcept;
    void discardCommandBufferUse(TrackedCommandBuffer& commandBuffer)noexcept;
    void releasePendingRecordingLease(u64 descriptorBufferGeneration)noexcept;
    void shutdownForDeviceTeardown()noexcept;
    void shutdownLocked();
    void resetStateForShutdownLocked()noexcept;
    [[nodiscard]] b8 initializeStorage(u32 resourceCapacity, u32 samplerCapacity, u32 accelStructCapacity);
    void publishBindingFastVersionLocked()noexcept{
        if(m_initialized.load(MemoryOrder::relaxed) && m_staticBindingContractValid && m_descriptorBufferGeneration != 0u){
            if(++m_nextBindingFastVersion == 0u)
                m_nextBindingFastVersion = 1u;
            m_bindingFastVersion.store(m_nextBindingFastVersion, MemoryOrder::release);
            return;
        }
        m_bindingFastVersion.store(0u, MemoryOrder::release);
    }
    void syncDynamicReadinessFlagLocked()noexcept{
        m_hasDynamicReadinessSlots.store(!m_dynamicReadinessSlots.empty(), MemoryOrder::release);
    }



private:
    Device& m_device;
    const VulkanContext& m_context;

    GpuDescriptorHeapDesc m_desc;

    BindingLayoutHandle m_resourceLayout;
    BindingLayoutHandle m_samplerLayout;
    BindingLayoutHandle m_accelStructLayout;

    Optional<Alloc::PersistentArena> m_storageArena;
    DescriptorBufferSegment m_resourceBufferBlock{};
    DescriptorBufferSegment m_samplerBufferBlock{};
    FixedTable<DescriptorBufferSegment> m_accelStructBufferBlocks;
    FixedTable<RayTracingAccelStructHandle> m_accelStructResources;
    FixedTable<BufferHandle> m_resourceDescriptorBuffers;
    FixedTable<TextureHandle> m_resourceDescriptorTextures;
    FixedTable<SamplerHandle> m_samplerDescriptorResources;
    Array<u32, GpuDescriptorClass::kCount> m_classBufferOffset = {};
    u32 m_accelStructBufferBindingOffset = 0u;

    SlotAllocator m_resourceSlots;
    SlotAllocator m_samplerSlots;
    SlotAllocator m_accelStructSlots;

    FixedTable<GpuDescriptorHandle> m_pendingRecording;
    FixedTable<RetiredSlot> m_retired;
    Vector<HeapUse, Alloc::GlobalArena> m_heapUses;
    Vector<GpuDescriptorHandle, Alloc::GlobalArena> m_dynamicReadinessSlots;
    Vector<GpuDescriptorHandle, Alloc::GlobalArena> m_concurrentAdmissionSlots;
    mutable Atomic<u64> m_verifiedConcurrentQueueMask{ 0u };
    usize m_pendingRecordingCount = 0u;
    usize m_retiredCount = 0u;
    usize m_activePendingRecordingLeaseCount = 0u;
    u64 m_lastHeapUseID = 0u;
    u64 m_descriptorBufferGeneration = 0u;
    u64 m_nextBindingFastVersion = 1u;
    Atomic<u64> m_bindingFastVersion{ 0u };
    Atomic<b8> m_hasDynamicReadinessSlots{ false };

    mutable Futex m_mutex;
    Atomic<b8> m_hasPendingRetirementWork{ false };
    Atomic<b8> m_initialized{ false };
    b8 m_staticBindingContractValid = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

