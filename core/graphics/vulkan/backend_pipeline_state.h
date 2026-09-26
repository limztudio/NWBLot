// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"
#include "backend_forward.h"

#include <core/task/cpu/scheduler.h>
#include "command_buffer_resource_references.h"
#include "heap_binding_contract.h"
#include "host_readback_sync.h"
#include "native_buffer_provenance.h"
#include "native_queue_state.h"
#include "native_texture_provenance.h"
#include "submitted_command_buffer_owner_lookup.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Pipeline binding


using PipelineShaderStageVector = Vector<VkPipelineShaderStageCreateInfo, Alloc::ScratchArena>;
using PipelineSpecializationInfoVector = Vector<VkSpecializationInfo, Alloc::ScratchArena>;

struct PipelineBindingState{
    BindingLayoutVector m_bindingLayoutsAtCreation;
    Array<u32, s_MaxBindingLayouts> m_bindingLayoutSetIndicesAtCreation{};
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    u32 m_pushConstantByteSize = 0;
    bool m_ownsPipelineLayout = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void DestroyPipelineResource(const VulkanContext& context, PipelineBindingState& state, VkPipeline& pipeline){
    DestroyPipelineAndOwnedLayout(
        context,
        pipeline,
        state.m_pipelineLayout,
        state.m_ownsPipelineLayout
    );
}

inline Object GetPipelineNativeHandle(const VkPipeline pipeline, const ObjectType objectType){
    if(objectType == ObjectTypes::VK_Pipeline)
        return Object(pipeline);
    return Object(nullptr);
}

inline void AttachPipelineBindingState(
    VkComputePipelineCreateInfo& pipelineInfo,
    const PipelineBindingState& bindingState,
    const void* next = nullptr
){
    pipelineInfo.pNext = next;
    pipelineInfo.layout = bindingState.m_pipelineLayout;
    pipelineInfo.flags |= VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
}

inline void AttachPipelineBindingState(
    VkGraphicsPipelineCreateInfo& pipelineInfo,
    const PipelineBindingState& bindingState,
    const void* next = nullptr
){
    pipelineInfo.pNext = next;
    pipelineInfo.layout = bindingState.m_pipelineLayout;
    pipelineInfo.flags |= VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Descriptor-buffer manager: host-mapped resource/sampler segments bind by byte offset.

namespace DescriptorBufferSegmentKind{
    static constexpr u8 kDescriptorBufferSegmentKindNoneBase = 0;
    enum Enum : u8{
        None = kDescriptorBufferSegmentKindNoneBase,
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

    [[nodiscard]] bool valid()const{
        return (kind == DescriptorBufferSegmentKind::Resource || kind == DescriptorBufferSegmentKind::Sampler)
            && sizeBytes > 0
            && storageIdentity != 0u
            && allocationSerial != 0u
        ;
    }
};

class DescriptorBufferManager final : NoCopy{
    friend class Device;
    friend class CommandList;
    friend class GpuDescriptorHeap;
    friend class Queue;


public:
    // Resource and sampler descriptor buffers are always bound in this order. A TLAS set reuses the resource
    // descriptor buffer, so the optional third offset extends this fixed topology.
    static constexpr u32 s_ResourceDescriptorBufferIndex = 0u;
    static constexpr u32 s_SamplerDescriptorBufferIndex = 1u;
    static constexpr u32 s_PersistentDescriptorBufferCount = 2u;
    static constexpr u32 s_DescriptorBufferCountWithAccelStruct = 3u;


private:
    // Mergeable free byte range.
    struct FreeRange{
        u32 offsetBytes = 0;
        u32 sizeBytes = 0;
    };

    // Host-mapped descriptor segment with synchronized free ranges and live-allocation tracking.
    struct SegmentStorage{
        const u64 storageIdentity;
        VkBuffer buffer = VK_NULL_HANDLE;
        VulkanAllocationHandle allocation = nullptr;
        void* mappedMemory = nullptr;
        VkDeviceAddress deviceAddress = 0;
        u32 capacityBytes = 0;
        u32 writableOffsetBytes = 0;
        // Preserve serial across reinitialization so stale handles stay invalid.
        u64 nextAllocationSerial = 1u;
        VkDescriptorBufferBindingInfoEXT bindingInfo{};
        Futex mutex;
        Vector<FreeRange, Alloc::GlobalArena> freeRanges;
        Vector<DescriptorBufferSegment, Alloc::GlobalArena> liveAllocations;


        SegmentStorage(Alloc::GlobalArena& arena, const u64 storageIdentityValue)
            : storageIdentity(storageIdentityValue)
            , freeRanges(arena)
            , liveAllocations(arena)
        {}
    };
    struct BindingSnapshot{
        Array<VkDescriptorBufferBindingInfoEXT, s_PersistentDescriptorBufferCount> bindingInfos{};
        u64 generation = 0u;
        u64 resourceStorageIdentity = 0u;
        u64 samplerStorageIdentity = 0u;
        u32 offsetAlignmentBytes = 0u;
    };


public:
    DescriptorBufferManager(Device& device, const VulkanContext& context, VulkanAllocator& allocator);
    ~DescriptorBufferManager()noexcept;


private:
    [[nodiscard]] bool shutdownForLifecycleOperation(VkResult& outIdleResult);
    void shutdownForDeviceTeardown()noexcept;


public:
    bool initialize();
    [[nodiscard]] bool shutdown();

    [[nodiscard]] bool isEnabled()const;

    // Exact driver descriptor size; 0 when descriptor buffers are disabled.
    [[nodiscard]] u32 getDescriptorSize(VkDescriptorType descriptorType)const;
    [[nodiscard]] u32 getOffsetAlignmentBytes()const;
    [[nodiscard]] u64 getUniformBufferAddressAlignmentBytes()const;
    [[nodiscard]] u64 getStorageBufferAddressAlignmentBytes()const;
    [[nodiscard]] u64 getTexelBufferAddressAlignmentBytes()const;
    [[nodiscard]] u32 getMaxTexelBufferElements()const;

    // Coherent copies from the current binding generation; zeroed while unavailable.
    [[nodiscard]] VkDescriptorBufferBindingInfoEXT getResourceBindingInfo()const;
    [[nodiscard]] VkDescriptorBufferBindingInfoEXT getSamplerBindingInfo()const;

    // Resource and sampler buffer indices in bind order.
    [[nodiscard]] u32 getResourceBufferIndex()const{ return s_ResourceDescriptorBufferIndex; }
    [[nodiscard]] u32 getSamplerBufferIndex()const{ return s_SamplerDescriptorBufferIndex; }

    // Allocates aligned, zeroed descriptor bytes from free ranges or the bump pointer.
    [[nodiscard]] DescriptorBufferSegment allocate(DescriptorBufferSegmentKind::Enum kind, u32 sizeBytes, u32 alignmentBytes);


private:
    [[nodiscard]] DescriptorBufferSegment allocateForBindingGeneration(
        DescriptorBufferSegmentKind::Enum kind,
        u32 sizeBytes,
        u32 alignmentBytes,
        u64 requiredGeneration
    );


public:
    void free(const DescriptorBufferSegment& segment);


private:
    void freeForBindingGeneration(const DescriptorBufferSegment& segment, u64 requiredGeneration);


public:
    // Validates allocation identity, then encodes one descriptor.
    bool writeDescriptor(const DescriptorWriteItem& item, const DescriptorBufferSegment& allocation, u32 dstOffsetBytes, VkDescriptorType descriptorType);


private:
    [[nodiscard]] bool captureBindingSnapshotLocked(BindingSnapshot& outSnapshot)const;
    [[nodiscard]] bool isLiveSegmentLocked(
        const SegmentStorage& storage,
        const DescriptorBufferSegment& segment,
        DescriptorBufferSegmentKind::Enum expectedKind
    )const;
    bool initializeSegment(SegmentStorage& segment, const ACompactString& debugName, u32 capacityBytes);
    void shutdownSegment(SegmentStorage& segment)noexcept;


private:
    Device& m_device;
    const VulkanContext& m_context;
    VulkanAllocator& m_allocator;
    Futex m_lifecycleOperationMutex;
    mutable Futex m_lifecycleMutex;
    u64 m_bindingGeneration = 0u;
    u64 m_nextBindingGeneration = 1u;
    bool m_enabled = false;
    bool m_lifecycleTransitioning = false;
    SegmentStorage m_resourceSegment;
    SegmentStorage m_samplerSegment;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Global Descriptor Heap


// Descriptor-buffer-only bindless heap at sets 0/1; required at device creation.
class GpuDescriptorHeap final : NoCopy{
    friend class Device;
    friend class CommandList;
    friend class Queue;
    friend class TrackedCommandBuffer;


private:
    // Capacities are fixed per initialized generation. The shared pool outlives
    // these typed allocations, including partially initialized tables.
    template<typename T>
    class FixedTable final : NoCopy{
    public:
        [[nodiscard]] static usize RequiredBytes(usize count){
            return count == 0u ? 0u : Alloc::PersistentArena::StructureAlignedSize(SizeOf<sizeof(T)>(count), alignof(T));
        }
        [[nodiscard]] bool initialize(Alloc::PersistentArena& arena, usize count){
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
        [[nodiscard]] T& operator[](usize index)noexcept{ return m_storage[index]; }
        [[nodiscard]] const T& operator[](usize index)const noexcept{ return m_storage[index]; }

    private:
        PersistentUniquePtr<T[]> m_storage;
        usize m_count = 0u;
    };

    enum class SlotState : u8{
        Free,
        Live,
        PendingRecording,
        Retired,
    };

    // Fresh indices plus a recycled free list per namespace.
    struct SlotAllocator{
        u32 capacity = 0;
        // Every allocated or quarantined slot lies below nextFresh; only clear() resets this high-water mark.
        u32 nextFresh = 0;
        FixedTable<u32> freeList;
        usize freeCount = 0u;
        // PendingRecording is the only freed state that native recording may still consume.
        FixedTable<SlotState> slotStates;
        // Keeps the allocated class authoritative while a slot is live or quarantined.
        FixedTable<u8> allocatedClasses;

        [[nodiscard]] static usize RequiredBytes(u32 capacity);
        [[nodiscard]] bool initialize(Alloc::PersistentArena& arena, u32 newCapacity);
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
    class PendingRecordingLease final{
        friend class GpuDescriptorHeap;


    private:
        explicit PendingRecordingLease(GpuDescriptorHeap& heap);

    public:
        ~PendingRecordingLease()noexcept;
        PendingRecordingLease(const PendingRecordingLease&) = delete;
        PendingRecordingLease(PendingRecordingLease&&) = delete;
        PendingRecordingLease& operator=(const PendingRecordingLease&) = delete;
        PendingRecordingLease& operator=(PendingRecordingLease&&) = delete;


    public:
        [[nodiscard]] bool valid()const noexcept{ return m_heap != nullptr; }

    private:
        GpuDescriptorHeap* m_heap = nullptr;
        u64 m_descriptorBufferGeneration = 0u;
    };


public:
    explicit GpuDescriptorHeap(Device& device);
    ~GpuDescriptorHeap()noexcept;


public:
    // Prevents slot reuse while CPU snapshots may still record. The final overlapping release establishes the
    // native heap-use boundary for every slot freed under the leases.
    [[nodiscard]] PendingRecordingLease acquirePendingRecordingLease();

    bool initialize(const GpuDescriptorHeapDesc& desc);
    void shutdown();

    [[nodiscard]] bool isInitialized()const{ return m_initialized; }
    // Coherent aggregate lifecycle counters; intentionally does not identify individual physical queues.
    [[nodiscard]] GpuDescriptorHeapLifecycleStatistics lifecycleStatistics()const;

    // Returns invalid (logged) when the class namespace is exhausted.
    [[nodiscard]] GpuDescriptorHandle allocate(GpuDescriptorClass::Enum descriptorClass);

    // Defers reuse across pending CPU recording and until every protected heap binding completes or is abandoned.
    void free(GpuDescriptorHandle handle);

    // Resolves deferred reuse from accepted queue tokens and their completed timelines.
    void collectRetired();

    // Caller must not overwrite a slot read by in-flight work.
    bool write(GpuDescriptorHandle handle, const DescriptorWriteItem& item);

    // Bind after state setup; optionally includes a per-generation TLAS block at set 2.
    void bindCompute(
        CommandList& commandList,
        const ComputePipeline& pipeline,
        GpuDescriptorHandle accelStructHandle = GpuDescriptorHandle::invalid()
    );

    // Graphics equivalents of bindCompute.
    void bindGraphics(CommandList& commandList, const GraphicsPipeline& pipeline);
    void bindGraphics(CommandList& commandList, const MeshletPipeline& pipeline);

    // Ray-tracing equivalent of bindCompute; optionally binds a TLAS block at set 2.
    void bindRayTracing(
        CommandList& commandList,
        const RayTracingPipeline& pipeline,
        GpuDescriptorHandle accelStructHandle = GpuDescriptorHandle::invalid()
    );

    [[nodiscard]] u32 getResourceCapacity()const{ return m_resourceSlots.capacity; }
    [[nodiscard]] u32 getSamplerCapacity()const{ return m_samplerSlots.capacity; }
    [[nodiscard]] u32 getResourceSetIndex()const{ return m_desc.bindlessHeapAbi.resourceSetIndex; }
    [[nodiscard]] u32 getSamplerSetIndex()const{ return m_desc.bindlessHeapAbi.samplerSetIndex; }
    [[nodiscard]] u32 getAccelStructSetIndex()const{ return m_desc.bindlessHeapAbi.accelStructSetIndex; }
    // Resource and sampler layouts at sets 0 and 1.
    [[nodiscard]] const BindingLayoutHandle& getResourceLayout()const{ return m_resourceLayout; }
    [[nodiscard]] const BindingLayoutHandle& getSamplerLayout()const{ return m_samplerLayout; }
    // Per-generation one-descriptor TLAS layout at set 2.
    [[nodiscard]] const BindingLayoutHandle& getAccelStructLayout()const{ return m_accelStructLayout; }
    [[nodiscard]] bool hasAccelStructLayout()const{ return m_accelStructLayout != nullptr; }
    // SPIR-V binding number for a descriptor class.
    [[nodiscard]] u32 getRegisterSlot(GpuDescriptorClass::Enum descriptorClass)const;
    // Persistent segment blocks bound at heap sets.
    [[nodiscard]] const DescriptorBufferSegment& getResourceBufferBlock()const{ return m_resourceBufferBlock; }
    [[nodiscard]] const DescriptorBufferSegment& getSamplerBufferBlock()const{ return m_samplerBufferBlock; }
    [[nodiscard]] DescriptorBufferSegment getAccelStructBufferBlock(GpuDescriptorHandle handle)const;


private:
    [[nodiscard]] SlotAllocator& allocatorForClass(GpuDescriptorClass::Enum descriptorClass)noexcept;
    void releaseAccelStructDescriptorBlock(u32 slot);
    void releaseRetainedDescriptorResource(GpuDescriptorHandle handle);
    [[nodiscard]] bool isResourceAdmittedToActiveUsesLocked(const ResourceQueueAdmissionSnapshot& admission)const noexcept;
    [[nodiscard]] bool retainedResourcesReadyForQueueLocked(const GpuPhysicalQueueInfo& queue)const noexcept;
    [[nodiscard]] bool retainedResourcesReadyForQueue(const GpuPhysicalQueueId& queue)const noexcept;
    [[nodiscard]] bool trackCommandBufferUseLocked(
        TrackedCommandBuffer& commandBuffer,
        const GpuPhysicalQueueId& physicalQueue
    );
    [[nodiscard]] bool validateCommandBufferUseSubmissionLocked(
        TrackedCommandBuffer& commandBuffer,
        const QueueSubmissionToken& submissionToken,
        usize& outHeapUseIndex
    );
    void commitCommandBufferUseSubmissionLocked(
        TrackedCommandBuffer& commandBuffer,
        const QueueSubmissionToken& submissionToken,
        usize heapUseIndex
    )noexcept;
    void discardCommandBufferUse(TrackedCommandBuffer& commandBuffer)noexcept;
    void releasePendingRecordingLease(u64 descriptorBufferGeneration)noexcept;
    void shutdownForDeviceTeardown()noexcept;
    void shutdownLocked();
    void resetStateForShutdownLocked()noexcept;
    [[nodiscard]] bool initializeStorage(u32 resourceCapacity, u32 samplerCapacity, u32 accelStructCapacity);

    // Allocates persistent resource/sampler blocks; TLAS blocks are per handle.
    bool initializeDescriptorBufferBlocks(u32 offsetAlignmentBytes);
    // Encodes one descriptor at the class/slot offset.
    bool writeDescriptorBuffer(const DescriptorWriteItem& writeItem, GpuDescriptorClass::Enum descriptorClass);


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
    u32 m_classBufferOffset[GpuDescriptorClass::kCount] = {};

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
    bool m_initialized = false;
    u32 m_accelStructBufferBindingOffset = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Graphics Pipeline


class GraphicsPipeline final : public RefCounter<GraphicsResource>, public PipelineBindingState, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    GraphicsPipeline(const VulkanContext& context);
    ~GraphicsPipeline();


public:
    [[nodiscard]] const GraphicsPipelineDesc& getDescription()const{ return m_desc; }
    [[nodiscard]] u16 getDeviceGeneration()const noexcept{ return m_context.deviceGeneration; }
    [[nodiscard]] const FramebufferInfo& getFramebufferInfo()const{ return m_framebufferInfo; }
    Object getNativeHandle(ObjectType objectType);


private:
    GraphicsPipelineDesc m_desc;
    FramebufferInfo m_framebufferInfo;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Compute Pipeline


class ComputePipeline final : public RefCounter<GraphicsResource>, public PipelineBindingState, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    ComputePipeline(const VulkanContext& context);
    ~ComputePipeline();


public:
    [[nodiscard]] const ComputePipelineDesc& getDescription()const{ return m_desc; }
    [[nodiscard]] u16 getDeviceGeneration()const noexcept{ return m_context.deviceGeneration; }
    Object getNativeHandle(ObjectType objectType);


private:
    ComputePipelineDesc m_desc;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Meshlet Pipeline


class MeshletPipeline final : public RefCounter<GraphicsResource>, public PipelineBindingState, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    MeshletPipeline(const VulkanContext& context);
    ~MeshletPipeline();


public:
    [[nodiscard]] const MeshletPipelineDesc& getDescription()const{ return m_desc; }
    [[nodiscard]] u16 getDeviceGeneration()const noexcept{ return m_context.deviceGeneration; }
    [[nodiscard]] const FramebufferInfo& getFramebufferInfo()const{ return m_framebufferInfo; }
    Object getNativeHandle(ObjectType objectType);


private:
    MeshletPipelineDesc m_desc;
    FramebufferInfo m_framebufferInfo;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Ray Tracing Pipeline


namespace ShaderTableRecordKind{
    enum Enum : u8{
        RayGeneration,
        Miss,
        HitGroup,
        Callable,

        Count,
        Invalid = Count,
    };
};

struct ShaderTableGroupMetadata{
    GraphicsString exportName;
    ShaderTableRecordKind::Enum kind = ShaderTableRecordKind::Invalid;
    u32 groupIndex = 0u;

    explicit ShaderTableGroupMetadata(GraphicsArena& arena)
        : exportName(arena)
    {}
};

class RayTracingPipeline final : public RefCounter<GraphicsResource>, public PipelineBindingState, NoCopy{
    friend class Device;
    friend class CommandList;
    friend class ShaderTable;


public:
    RayTracingPipeline(const VulkanContext& context, Device& device, bool allowClusterAccelerationStructures);
    ~RayTracingPipeline();


public:
    [[nodiscard]] const RayTracingPipelineDesc& getDescription()const{ return m_desc; }
    [[nodiscard]] u16 getDeviceGeneration()const noexcept{ return m_context.deviceGeneration; }
    [[nodiscard]] bool allowsClusterAccelerationStructures()const noexcept{ return m_allowClusterAccelerationStructuresAtCreation; }
    [[nodiscard]] RayTracingShaderTableHandle createShaderTable();
    Object getNativeHandle(ObjectType objectType);


private:
    RayTracingPipelineDesc m_desc;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    // vkGetRayTracingShaderGroupHandlesKHR returns tightly packed handles; SBT records add alignment later.
    Vector<u8, Alloc::GlobalArena> m_shaderGroupHandles;
    GraphicsVector<ShaderTableGroupMetadata> m_shaderGroups;

    const bool m_allowClusterAccelerationStructuresAtCreation;
    const VulkanContext& m_context;
    Device& m_device;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shader Table


class ShaderTable final : public RefCounter<GraphicsResource>, NoCopy{
    friend class CommandList;
    friend class RayTracingPipeline;


private:
    struct DispatchRegionSnapshot{
        BufferHandle buffer;
        u64 offset = 0u;
        u32 recordCount = 0u;
        usize selectedGroupCount = 0u;
    };

    struct DispatchSnapshot{
        Handle<RayTracingPipeline> pipeline;
        DispatchRegionSnapshot rayGeneration;
        DispatchRegionSnapshot miss;
        DispatchRegionSnapshot hit;
        DispatchRegionSnapshot callable;
    };

    struct ShaderRecordPreflight{
        usize handleOffset = 0u;
        u64 recordByteSize = 0u;
        u64 allocationByteSize = 0u;
        u32 groupIndex = 0u;
        u32 handleSize = 0u;
        u32 handleSizeAligned = 0u;
        u32 baseAlignment = 0u;
    };


public:
    ShaderTable(const VulkanContext& context, Device& device);
    ~ShaderTable();


public:
    [[nodiscard]] bool setRayGenerationShader(AStringView exportName);
    [[nodiscard]] u32 addMissShader(AStringView exportName);
    [[nodiscard]] u32 addHitGroup(AStringView exportName);
    [[nodiscard]] u32 addCallableShader(AStringView exportName);
    void clearMissShaders();
    void clearHitShaders();
    void clearCallableShaders();
    [[nodiscard]] RayTracingPipeline* getPipeline(){ return m_pipeline.get(); }
    Object getNativeHandle(ObjectType objectType);


private:
    void captureDispatchSnapshot(DispatchSnapshot& outSnapshot)const;
    [[nodiscard]] bool findGroupIndex(
        AStringView exportName,
        ShaderTableRecordKind::Enum expectedKind,
        u32& outGroupIndex,
        const tchar* operationName,
        const tchar* exportKind
    )const;
    [[nodiscard]] bool preflightShaderRecord(
        AStringView exportName,
        ShaderTableRecordKind::Enum expectedKind,
        u32 recordCount,
        ShaderRecordPreflight& outPreflight,
        const tchar* operationName,
        const tchar* exportKind
    )const;
    [[nodiscard]] bool allocateSBTBuffer(
        const ShaderRecordPreflight& preflight,
        BufferHandle& outBuffer,
        u64& outOffset,
        const tchar* operationName,
        const tchar* recordName
    );
    [[nodiscard]] u32 appendShaderRecord(
        AStringView exportName,
        ShaderTableRecordKind::Enum expectedKind,
        GraphicsVector<u32>& groupIndices,
        BufferHandle& buffer,
        u64& offset,
        u32& count,
        const tchar* operationName,
        const tchar* recordName,
        const tchar* exportKind
    );


private:
    Handle<RayTracingPipeline> m_pipeline;

    BufferHandle m_raygenBuffer;
    BufferHandle m_missBuffer;
    BufferHandle m_hitBuffer;
    BufferHandle m_callableBuffer;

    GraphicsVector<u32> m_missGroupIndices;
    GraphicsVector<u32> m_hitGroupIndices;
    GraphicsVector<u32> m_callableGroupIndices;

    u64 m_raygenOffset = 0;
    u64 m_missOffset = 0;
    u64 m_hitOffset = 0;
    u64 m_callableOffset = 0;

    mutable Futex m_mutex;

    u32 m_missCount = 0;
    u32 m_hitCount = 0;
    u32 m_callableCount = 0;

    const VulkanContext& m_context;
    Device& m_device;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

