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


// Descriptor-buffer-only bindless heap at sets 0/1; required at device creation.
class GpuDescriptorHeap final : NoCopy{
    friend class Device;
    friend class CommandList;
    friend class Queue;
    friend class TrackedCommandBuffer;


private:
    class DescriptorBufferWriter;

    // Capacities are fixed per initialized generation.
    // The shared pool outlives these typed allocations, including partially initialized tables.
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

    // Fresh indices plus a recycled free list per namespace.
    struct SlotAllocator{
        u32 capacity = 0;
        // Every allocated or quarantined slot lies below nextFresh; only clear() resets this high-water mark.
        u32 nextFresh = 0;
        FixedTable<u32> freeList;
        usize freeCount = 0u;
        // PendingRecording is the only freed state that native recording may still consume.
        FixedTable<SlotState::Enum> slotStates;
        // Keeps the allocated class authoritative while a slot is live or quarantined.
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
    // Prevents slot reuse while CPU snapshots may still record.
    // The final overlapping release establishes the native heap-use boundary for every slot freed under the leases.
    [[nodiscard]] PendingRecordingLease acquirePendingRecordingLease();

    b8 initialize(const GpuDescriptorHeapDesc& desc);
    void shutdown();

    [[nodiscard]] b8 isInitialized()const noexcept{ return m_initialized; }
    // Coherent aggregate lifecycle counters; intentionally does not identify individual physical queues.
    [[nodiscard]] GpuDescriptorHeapLifecycleStatistics lifecycleStatistics()const noexcept;

    // Fails (logged) when the heap is not ready or the class namespace is exhausted.
    [[nodiscard]] Expected<GpuDescriptorHandle> allocate(GpuDescriptorClass::Enum descriptorClass);

    // Defers reuse across pending CPU recording and until every protected heap binding completes or is abandoned.
    void free(GpuDescriptorHandle handle);

    // Resolves deferred reuse from accepted queue tokens and their completed timelines.
    void collectRetired();

    // Caller must not overwrite a slot read by in-flight work.
    b8 write(GpuDescriptorHandle handle, const DescriptorWriteItem& item);

    // Bind after state setup; optionally includes a per-generation TLAS block at set 2.
    void bindCompute(CommandList& commandList, const ComputePipeline& pipeline, GpuDescriptorHandle accelStructHandle = GpuDescriptorHandle::Invalid());

    // Graphics equivalents of bindCompute.
    void bindGraphics(CommandList& commandList, const GraphicsPipeline& pipeline);
    void bindGraphics(CommandList& commandList, const MeshletPipeline& pipeline);

    // Ray-tracing equivalent of bindCompute; optionally binds a TLAS block at set 2.
    void bindRayTracing(CommandList& commandList, const RayTracingPipeline& pipeline, GpuDescriptorHandle accelStructHandle = GpuDescriptorHandle::Invalid());

    // Resource and sampler layouts at sets 0 and 1.
    [[nodiscard]] const BindingLayoutHandle& getResourceLayout()const noexcept{ return m_resourceLayout; }

    [[nodiscard]] const BindingLayoutHandle& getSamplerLayout()const noexcept{ return m_samplerLayout; }
    // Per-generation one-descriptor TLAS layout at set 2.
    [[nodiscard]] const BindingLayoutHandle& getAccelStructLayout()const noexcept{ return m_accelStructLayout; }

    [[nodiscard]] b8 hasAccelStructLayout()const noexcept{ return m_accelStructLayout != nullptr; }
    // SPIR-V binding number for a descriptor class.
    [[nodiscard]] u32 getRegisterSlot(GpuDescriptorClass::Enum descriptorClass)const noexcept;


private:
    [[nodiscard]] SlotAllocator& allocatorForClass(GpuDescriptorClass::Enum descriptorClass)noexcept;
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



private:
    Device& m_device;
    const VulkanContext& m_context;

    GpuDescriptorHeapDesc m_desc;

    BindingLayoutHandle m_resourceLayout;
    BindingLayoutHandle m_samplerLayout;
    BindingLayoutHandle m_accelStructLayout;

    // The pool is destroyed after every table, including during unwinding.
    Optional<Alloc::PersistentArena> m_storageArena;
    DescriptorBufferSegment m_resourceBufferBlock{};
    DescriptorBufferSegment m_samplerBufferBlock{};
    // TLAS blocks are immutable per generation until deferred free.
    FixedTable<DescriptorBufferSegment> m_accelStructBufferBlocks;
    FixedTable<RayTracingAccelStructHandle> m_accelStructResources;
    // Resource keep-alives protect descriptors used by in-flight work.
    FixedTable<BufferHandle> m_resourceDescriptorBuffers;
    FixedTable<TextureHandle> m_resourceDescriptorTextures;
    FixedTable<SamplerHandle> m_samplerDescriptorResources;
    // Binding byte offsets within a set block.
    Array<u32, GpuDescriptorClass::Count> m_classBufferOffset = {};
    u32 m_accelStructBufferBindingOffset = 0u;

    SlotAllocator m_resourceSlots;
    SlotAllocator m_samplerSlots;
    SlotAllocator m_accelStructSlots;

    FixedTable<GpuDescriptorHandle> m_pendingRecording;
    FixedTable<RetiredSlot> m_retired;
    Vector<HeapUse, Alloc::GlobalArena> m_heapUses;
    usize m_pendingRecordingCount = 0u;
    usize m_retiredCount = 0u;
    usize m_activePendingRecordingLeaseCount = 0u;
    u64 m_lastHeapUseID = 0u;
    u64 m_descriptorBufferGeneration = 0u;

    mutable Futex m_mutex;
    b8 m_initialized = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

