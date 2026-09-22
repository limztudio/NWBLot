// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"

#include <core/task/cpu/scheduler.h>
#include "command_buffer_resource_references.h"
#include "heap_binding_contract.h"
#include "host_readback_sync.h"
#include "native_buffer_provenance.h"
#include "native_queue_state.h"
#include "native_texture_provenance.h"
#include "submitted_command_buffer_owner_lookup.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Device;
class VulkanTestDispatchAccess;
class Queue;
class TrackedCommandBuffer;
class StateTracker;
class GpuDescriptorHeap;
class DescriptorBufferManager;

class Buffer;
class Texture;
class AccelStruct;
class OpacityMicromap;

struct VulkanContext;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Device Implementation


class Device final : public RefCounter<GraphicsResource>, NoCopy{
    friend DeviceHandle CreateDevice(const DeviceDesc& desc);
    friend class ::NWB::Core::GpuTaskScheduler;
    friend class VulkanTestDispatchAccess;
    friend class BackendContext;
    friend class Buffer;
    friend class CommandList;
    friend class DescriptorBufferManager;
    friend class Queue;
    friend class Texture;
    friend class UploadManager;
    friend class GpuDescriptorHeap;


private:
    enum class DeviceLossDiagnosticPolicy : u8{
        Capture,
        Defer,
    };
    enum class SubmissionCommandListValidationPolicy : u8{
        Prevalidated,
        ValidateWithinWorkspace,
    };


private:
    static constexpr u64 s_SubmissionDrainBit = static_cast<u64>(1u) << 63u;
    static constexpr u64 s_SubmissionOperationCountMask = s_SubmissionDrainBit - 1u;


private:
    class SubmissionOperationLease final : NoCopy{
    private:
        [[nodiscard]] static const SubmissionOperationLease*& activeLease()noexcept{
            static thread_local const SubmissionOperationLease* value = nullptr;
            return value;
        }


    public:
        [[nodiscard]] static bool activeFor(const Device& device)noexcept{
            for(const SubmissionOperationLease* lease = activeLease(); lease; lease = lease->m_previousActiveLease){
                if(lease->m_device == &device)
                    return true;
            }
            return false;
        }


    public:
        explicit SubmissionOperationLease(Device& device)noexcept{
            if(!device.beginSubmissionOperation())
                return;
            m_device = &device;
            m_previousActiveLease = activeLease();
            activeLease() = this;
        }
        ~SubmissionOperationLease()noexcept;


    public:
        [[nodiscard]] bool valid()const noexcept{ return m_device != nullptr; }


    private:
        Device* m_device = nullptr;
        const SubmissionOperationLease* m_previousActiveLease = nullptr;
    };

    // AMD breadcrumb ring provides best-effort per-queue observations; physical queues may execute out of sequence.
    struct AmdBreadcrumbSlotRecord{
        u64 serial = 0u;
        usize markerHash = 0u;
        u32 marker = 0u;
    };
    struct AmdBreadcrumbBuffer{
        struct Metadata{
            Alloc::PersistentArena arena;
            PersistentUniquePtr<AmdBreadcrumbSlotRecord[]> slotRecords;
            PersistentUniquePtr<u64[]> nextSerials;
            usize slotRecordCount = 0u;
            usize nextSerialCount = 0u;

            Metadata(
                const Name& allocationLog,
                const usize arenaBytes,
                const usize newSlotRecordCount,
                const usize newNextSerialCount
            )
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
        VulkanDetail::AmdBreadcrumbRingLayout layout;
        // Prevents concurrent recording from tearing marker records or per-queue reservation serials.
        Futex slotMutex;
        Optional<Metadata> metadata;

        // The caller supplies a validated, non-empty ring layout. Both arrays allocate once and retain their fixed
        // device-lifetime backing, so their exact allocation spans are sufficient for the private persistent arena.
        [[nodiscard]] bool initializeMetadata(
            const VulkanDetail::AmdBreadcrumbRingLayout& newLayout,
            const Name& allocationLog
        ){
            usize expectedSlotCount = 0u;
            if(
                metadata
                || newLayout.physicalQueueCount == 0u
                || newLayout.slotsPerQueue == 0u
                || !TryMultiply<usize>(
                    newLayout.physicalQueueCount,
                    newLayout.slotsPerQueue,
                    expectedSlotCount
                )
                || expectedSlotCount != newLayout.totalSlotCount
            )
                return false;

            usize slotRecordBytes = 0u;
            usize nextSerialBytes = 0u;
            if(
                !TryMultiply<usize>(
                    newLayout.totalSlotCount,
                    sizeof(AmdBreadcrumbSlotRecord),
                    slotRecordBytes
                )
                || !TryMultiply<usize>(
                    newLayout.physicalQueueCount,
                    sizeof(u64),
                    nextSerialBytes
                )
            )
                return false;

            const usize slotRecordAllocationBytes = Alloc::PersistentArena::StructureAlignedSize(
                slotRecordBytes,
                alignof(AmdBreadcrumbSlotRecord)
            );
            const usize nextSerialAllocationBytes = Alloc::PersistentArena::StructureAlignedSize(
                nextSerialBytes,
                alignof(u64)
            );
            if(AddOverflows<usize>(slotRecordAllocationBytes, nextSerialAllocationBytes))
                return false;

            metadata.emplace(
                allocationLog,
                slotRecordAllocationBytes + nextSerialAllocationBytes,
                newLayout.totalSlotCount,
                newLayout.physicalQueueCount
            );
            if(!metadata->slotRecords || !metadata->nextSerials){
                metadata.reset();
                return false;
            }

            for(usize slotIndex = 0u; slotIndex < metadata->slotRecordCount; ++slotIndex)
                metadata->slotRecords[slotIndex] = {};
            for(usize queueIndex = 0u; queueIndex < metadata->nextSerialCount; ++queueIndex)
                metadata->nextSerials[queueIndex] = 0u;
            layout = newLayout;
            return true;
        }
    };


public:
    // Reserves a marker slot; invalid when AMD breadcrumbs are unavailable.
    struct AmdBreadcrumbWrite{
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset = 0u;
        u32 marker = 0u;
        bool valid = false;
    };


public:
    explicit Device(const DeviceDesc& desc);
    virtual ~Device()noexcept override;


public:
    [[nodiscard]] HeapHandle createHeap(const HeapDesc& d);
    [[nodiscard]] TextureHandle createTexture(const TextureDesc& d);
    [[nodiscard]] MemoryRequirements getTextureMemoryRequirements(Texture& texture);
    bool bindTextureMemory(Texture& texture, Heap& heap, u64 offset);
    // Nonlogging backing-readiness snapshot for command/packet preflight; this is not a synchronization guarantee.
    [[nodiscard]] bool isTextureReadyForGpuUse(
        Texture* texture,
        VkImageUsageFlags requiredUsage = 0u
    )const noexcept;
    // The caller owns native binding and lifetime and must provide exact immutable creation provenance.
    // Only one live Texture wrapper may name a VkImage per Device.
    [[nodiscard]] TextureHandle createHandleForNativeTexture(
        ObjectType objectType,
        Object texture,
        const TextureDesc& desc,
        const NativeTextureProvenance& nativeProvenance
    );
    [[nodiscard]] StagingTextureHandle createStagingTexture(const TextureDesc& d, CpuAccessMode::Enum cpuAccess);
    void* mapStagingTexture(StagingTexture& tex, const TextureSlice& slice, CpuAccessMode::Enum, usize* outRowPitch);
    void unmapStagingTexture(StagingTexture& tex);
    [[nodiscard]] BufferHandle createBuffer(const BufferDesc& d);
    void* mapBuffer(Buffer& buffer, CpuAccessMode::Enum);
    void unmapBuffer(Buffer& buffer);
    [[nodiscard]] MemoryRequirements getBufferMemoryRequirements(Buffer& buffer);
    bool bindBufferMemory(Buffer& buffer, Heap& heap, u64 offset);
    // Nonlogging backing-readiness snapshot for command/packet preflight; this is not a synchronization guarantee.
    // Native wrappers trust caller-managed binding. Managed ordinary buffers require their VMA allocation, while
    // managed virtual buffers require a retained, device-owned bound Heap allocation. CPU mapping is irrelevant.
    [[nodiscard]] bool isBufferReadyForGpuUse(Buffer* buffer, VkBufferUsageFlags requiredUsage = 0u)const noexcept;
    // The caller owns native binding and lifetime and must provide the exact immutable creation provenance.
    // Only one live Buffer wrapper may name a VkBuffer per Device.
    [[nodiscard]] BufferHandle createHandleForNativeBuffer(
        ObjectType objectType,
        Object buffer,
        const BufferDesc& desc,
        const NativeBufferProvenance& nativeProvenance
    );
    [[nodiscard]] ShaderHandle createShader(const ShaderDesc& d, const void* binary, usize binarySize);
    [[nodiscard]] ShaderHandle createShaderSpecialization(Shader& baseShader, const ShaderSpecialization* constants, u32 numConstants);
    [[nodiscard]] ShaderLibraryHandle createShaderLibrary(const void* binary, usize binarySize);
    [[nodiscard]] SamplerHandle createSampler(const SamplerDesc& d);
    [[nodiscard]] InputLayoutHandle createInputLayout(const VertexAttributeDesc* d, u32 attributeCount, Shader*);
    [[nodiscard]] EventQueryHandle createEventQuery();
    [[nodiscard]] bool setEventQuery(EventQuery& query, CommandQueue::Enum queue);
    [[nodiscard]] bool pollEventQuery(EventQuery& query);
    [[nodiscard]] bool waitEventQuery(EventQuery& query);
    [[nodiscard]] TimerQueryHandle createTimerQuery();
    bool pollTimerQuery(TimerQuery& query);
    [[nodiscard]] bool getTimerQueryResult(TimerQuery& query, TimerQueryResult& outResult);
    f32 getTimerQueryTime(TimerQuery& query);
    bool resetTimerQuery(TimerQuery& query);
    [[nodiscard]] FramebufferHandle createFramebuffer(const FramebufferDesc& desc);
    [[nodiscard]] GraphicsPipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc& desc, FramebufferInfo const& fbinfo);
    [[nodiscard]] ComputePipelineHandle createComputePipeline(const ComputePipelineDesc& desc);
    [[nodiscard]] MeshletPipelineHandle createMeshletPipeline(const MeshletPipelineDesc& desc, FramebufferInfo const& fbinfo);
    [[nodiscard]] RayTracingPipelineHandle createRayTracingPipeline(const RayTracingPipelineDesc& desc);
    [[nodiscard]] BindingLayoutHandle createBindingLayout(const BindingLayoutDesc& desc);
    [[nodiscard]] BindingLayoutHandle createBindlessLayout(const BindlessLayoutDesc& desc);
    [[nodiscard]] RayTracingOpacityMicromapHandle createOpacityMicromap(const RayTracingOpacityMicromapDesc& desc);
    [[nodiscard]] RayTracingAccelStructHandle createAccelStruct(const RayTracingAccelStructDesc& desc);
    [[nodiscard]] MemoryRequirements getAccelStructMemoryRequirements(RayTracingAccelStruct& as);
    [[nodiscard]] RayTracingClusterOperationSizeInfo getClusterOperationSizeInfo(const RayTracingClusterOperationParams& params);
    bool bindAccelStructMemory(RayTracingAccelStruct& as, Heap& heap, u64 offset);
    // Structural readiness deliberately allows an unbuilt acceleration structure to remain a legal build target.
    [[nodiscard]] bool isAccelStructReadyForGpuUse(RayTracingAccelStruct* as)const noexcept;
    [[nodiscard]] CommandListHandle createCommandList(const CommandListParameters& params = CommandListParameters());
    // outCommandListsSubmitted is true only for a new command-buffer submission.
    u64 executeCommandLists(
        CommandList* const* pCommandLists,
        usize numCommandLists,
        CommandQueue::Enum executionQueue = CommandQueue::Graphics,
        bool* outCommandListsSubmitted = nullptr
    );
    u64 executeCommandLists(
        CommandList* const* pCommandLists,
        usize numCommandLists,
        const GpuPhysicalQueueId& executionQueue,
        bool* outCommandListsSubmitted = nullptr
    );
    // Cross-queue dependencies are immutable submission-local token edges.
    [[nodiscard]] QueueSubmissionToken executeCommandLists(
        CommandList* const* pCommandLists,
        usize numCommandLists,
        CommandQueue::Enum executionQueue,
        const QueueSubmissionDesc& submitDesc
    );
    [[nodiscard]] QueueSubmissionToken executeCommandLists(
        CommandList* const* pCommandLists,
        usize numCommandLists,
        const GpuPhysicalQueueId& executionQueue,
        const QueueSubmissionDesc& submitDesc
    );


private:
    [[nodiscard]] bool prepareSubmissionCommandListWorkspaceLocked(
        Queue& queue,
        CommandList* const* pCommandLists,
        usize numCommandLists,
        const GpuPhysicalQueueId& executionQueue,
        bool graphSubmissionAuthorized,
        SubmissionCommandListValidationPolicy validationPolicy,
        bool* outHasSubmittedOwner = nullptr
    );
    void finalizeSubmissionCommandListResourcesLocked(
        Queue& queue,
        CommandList* const* pCommandLists,
        usize numCommandLists,
        const GpuPhysicalQueueId& executionQueue,
        u64 submittedID,
        bool submissionAccepted
    );
    [[nodiscard]] QueueSubmissionToken executeGraphCommandLists(
        CommandList* const* pCommandLists,
        usize numCommandLists,
        const GpuPhysicalQueueId& executionQueue,
        const QueueSubmissionDesc& submitDesc
    );
    [[nodiscard]] QueueSubmissionToken executeCommandListsInternal(
        CommandList* const* pCommandLists,
        usize numCommandLists,
        const GpuPhysicalQueueId& executionQueue,
        const QueueSubmissionDesc& submitDesc,
        bool graphSubmissionAuthorized,
        DeviceLossDiagnosticPolicy deviceLossDiagnosticPolicy = DeviceLossDiagnosticPolicy::Capture
    );


public:
    // The registry owns every active native VkQueue. Broad CommandQueue calls resolve through the designated
    // primary record only for legacy callers; graph recording/submission selects a concrete ID directly.
    [[nodiscard]] u16 getDeviceGeneration()const noexcept{ return m_deviceGeneration; }
    [[nodiscard]] u16 getPhysicalQueueIndex(CommandQueue::Enum queue)const noexcept;
    [[nodiscard]] GpuPhysicalQueueId getPrimaryPhysicalQueue(CommandQueue::Enum queue)const noexcept;
    [[nodiscard]] GpuPhysicalQueueTopology getPhysicalQueueTopology()const noexcept;
    [[nodiscard]] const GpuPhysicalQueueInfo* getPhysicalQueueInfo(const GpuPhysicalQueueId& queue)const noexcept;
    [[nodiscard]] GpuQueueTimelineSnapshot getQueueTimelineSnapshot(const GpuPhysicalQueueId& queue);
    [[nodiscard]] bool supportsComparableGpuTimestamps()const noexcept{ return m_context.comparableGpuTimestamps; }
    // Absolute ranges require the extension-backed logical-device epoch and a complete 64-bit queue timestamp.
    // Partial-width timestamps remain valid for modular ordinary durations only.
    [[nodiscard]] bool supportsComparableGpuTimestamps(const GpuPhysicalQueueId& queue)const noexcept{
        const GpuPhysicalQueueInfo* const queueInfo = getPhysicalQueueInfo(queue);
        return supportsComparableGpuTimestamps() && queueInfo && queueInfo->timestampValidBits == 64u;
    }
    [[nodiscard]] GpuCommandArenaStatistics getCommandArenaStatistics(const GpuPhysicalQueueId& queue)const noexcept;
    [[nodiscard]] GpuCommandArenaWorkerStatistics getCommandArenaWorkerStatistics(
        const GpuPhysicalQueueId& queue,
        u64 recordingWorkerDomain,
        u32 recordingWorkerIndex
    )const noexcept;
    [[nodiscard]] bool matchesPhysicalQueueIdentity(
        CommandQueue::Enum queue,
        u16 physicalQueueIndex,
        u16 deviceGeneration
    )const noexcept;
    [[nodiscard]] bool matchesPhysicalQueueIdentity(const GpuPhysicalQueueId& queue)const noexcept;
    // Validates an accepted token as a current submission wait without changing queue state. The producer timeline
    // is sampled under its queue mutex so a failed submit cannot expose its tentative value as available work.
    [[nodiscard]] bool validateSubmissionWaitToken(const QueueSubmissionToken& token)const noexcept;
    // Blocks until one exact accepted queue timeline value completes. This is used to prove that a bridged WSI
    // binary semaphore wait has consumed its signal before the acquire slot is reset and reused.
    [[nodiscard]] bool waitForSubmissionToken(const QueueSubmissionToken& token);
    // Native device loss permits loss-aware teardown. Logical quarantine still requires an idle join before freeing.
    [[nodiscard]] bool isDeviceLost()const noexcept{ return m_deviceLost.load(MemoryOrder::acquire); }
    [[nodiscard]] bool requiresRecreation()const noexcept{
        return isDeviceLost() || m_deviceQuarantined.load(MemoryOrder::acquire);
    }
    void quarantineDevice()noexcept{ m_deviceQuarantined.store(true, MemoryOrder::release); }
    // Reports core Graphics+Compute timestamp-stage support only; it does not imply that distinct submissions share
    // a comparable timestamp epoch. Use supportsComparableGpuTimestamps() for absolute ranges.
    [[nodiscard]] bool supportsGraphicsAndComputeTimestamps()const{
        return m_context.physicalDeviceProperties.limits.timestampComputeAndGraphics == VK_TRUE;
    }
    [[nodiscard]] u32 getQueueFamilyIndex(CommandQueue::Enum queue)const;
    [[nodiscard]] u32 getQueueFamilyIndex(const GpuPhysicalQueueId& queue)const;
    [[nodiscard]] bool usesConcurrentQueueSharing(ResourceQueueSharing::Mask sharing)const{
        return UsesConcurrentQueueSharing(sharing, m_context);
    }
    bool waitForIdle();
    void runGarbageCollection();
    bool queryFeatureSupport(Feature::Enum feature, void* = nullptr, usize = 0);
    // Maximum byte range represented by one storage-buffer descriptor; independent of allocation/VRAM budgets.
    [[nodiscard]] u64 getMaxStorageBufferRange()const noexcept;
    [[nodiscard]] FormatSupport::Mask queryFormatSupport(Format::Enum format);
    [[nodiscard]] CooperativeVectorDeviceFeatures queryCoopVecFeatures();
    usize getCoopVecMatrixSize(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, i32 rows, i32 columns);
    // Borrowed interoperability identity only. External queue operations remain the caller's responsibility and
    // must not overlap engine submission, presentation, idle, or teardown.
    [[nodiscard]] Object getNativeQueue(ObjectType objectType, CommandQueue::Enum queue);
    [[nodiscard]] Object getNativeQueue(ObjectType objectType, const GpuPhysicalQueueId& queue);
    bool isGpuCrashDiagnosticsEnabled()const noexcept{ return m_gpuCrashDiagnosticsEnabled && m_context.extensions.NV_device_diagnostic_checkpoints; }
    bool isAmdBreadcrumbEnabled()const noexcept{ return m_gpuCrashDiagnosticsEnabled && m_context.extensions.AMD_buffer_marker && m_amdBreadcrumb.metadata && m_amdBreadcrumb.buffer != VK_NULL_HANDLE; }
    // NV and AMD marker paths share one command-list tracker.
    bool isAnyGpuMarkerEnabled()const noexcept{ return isGpuCrashDiagnosticsEnabled() || isAmdBreadcrumbEnabled(); }
    [[nodiscard]] GpuCrashTracker& getGpuCrashTracker(){ return m_gpuCrashTracker; }
    void captureDeviceLoss(AStringView context);

    [[nodiscard]] AmdBreadcrumbWrite reserveAmdBreadcrumb(
        const GpuPhysicalQueueId& queue,
        usize markerHash
    );

    void queueWaitForSemaphore(CommandQueue::Enum waitQueue, VkSemaphore semaphore, u64 value);
    void queueSignalSemaphore(CommandQueue::Enum executionQueue, VkSemaphore semaphore, u64 value);
    [[nodiscard]] u64 queueGetCompletedInstance(CommandQueue::Enum queue);
    [[nodiscard]] u64 queueGetCompletedInstance(const GpuPhysicalQueueId& queue);
    void queueWaitForCommandList(CommandQueue::Enum waitQueue, CommandQueue::Enum executionQueue, u64 instance);

public:
    [[nodiscard]] Queue* getQueue(CommandQueue::Enum queueType);
    [[nodiscard]] Queue* getQueue(const GpuPhysicalQueueId& queue);
    [[nodiscard]] GpuDescriptorHeap& getDescriptorHeap(){ return m_gpuDescriptorHeap; }
    // Writes descriptor-buffer entries, including TLAS handles.
    [[nodiscard]] DescriptorBufferManager& getDescriptorBufferManager(){ return m_descriptorBufferManager; }


private:
    void markDeviceLost()noexcept{ m_deviceLost.store(true, MemoryOrder::release); }
    [[nodiscard]] VkResult waitForNativeIdle()noexcept;
    [[nodiscard]] bool waitForSubmissionTokenInternal(
        const QueueSubmissionToken& token,
        DeviceLossDiagnosticPolicy deviceLossDiagnosticPolicy
    );
    [[nodiscard]] bool setEventQueryInternal(
        EventQuery& query,
        CommandQueue::Enum queue,
        DeviceLossDiagnosticPolicy deviceLossDiagnosticPolicy
    );
    [[nodiscard]] bool pollEventQueryInternal(
        EventQuery& query,
        DeviceLossDiagnosticPolicy deviceLossDiagnosticPolicy
    );
    [[nodiscard]] bool waitEventQueryInternal(
        EventQuery& query,
        DeviceLossDiagnosticPolicy deviceLossDiagnosticPolicy
    );
    void prepareForDestructionAfterIdleOrLoss();
    [[nodiscard]] bool beginSubmissionOperation()noexcept;
    void endSubmissionOperation()noexcept;
    [[nodiscard]] bool submissionOperationActiveOnCurrentThread()const noexcept{
        return SubmissionOperationLease::activeFor(*this);
    }
    [[nodiscard]] bool beginLifecycleDrain()noexcept;
    void endLifecycleDrain()noexcept;
    // Records the prepare phase's idle-or-loss proof. Once sealed, the submission gate remains closed and the
    // destructor must not issue another fallible Vulkan wait during the commit phase.
    [[nodiscard]] bool sealLifecycleDrainForDestruction()noexcept;
    [[nodiscard]] bool submissionsBlocked()const noexcept{
        return requiresRecreation() || (m_submissionOperationState.load(MemoryOrder::acquire) & s_SubmissionDrainBit) != 0u;
    }
    [[nodiscard]] QueueSubmissionToken consumeAcquiredImageSemaphore(VkSemaphore semaphore);
    [[nodiscard]] bool presentNativeQueue(
        u32 nativeQueueIndex,
        const VkPresentInfoKHR& presentInfo,
        VkResult& outResult
    );
    [[nodiscard]] bool registerPhysicalQueue(
        const VulkanPhysicalQueueDesc& desc,
        NativeQueueState& nativeQueue
    );
    void configureLegacyQueueContext();
    // Probed once at device initialization so compressed texture selection does not rely on
    // a later optimistic format-property query.
    void probeCompressedTextureFormats();
    [[nodiscard]] FormatSupport::Mask queryFormatSupportUncached(Format::Enum format)const;
    [[nodiscard]] bool canCreateSampledTextureFormat(Format::Enum format)const;
    [[nodiscard]] bool loadPipelineCacheData(GraphicsBytes& outData);
    void savePipelineCacheData();
    [[nodiscard]] bool createPipelineLayoutForBindingLayouts(
        const BindingLayoutVector& bindingLayouts,
        const tchar* operationName,
        VkPipelineLayout& outPipelineLayout,
        u32& outPushConstantByteSize,
        bool& outOwnsPipelineLayout,
        Alloc::ScratchArena& scratchArena
    )const;
    [[nodiscard]] bool validateHeapMemoryBinding(
        const Heap& heap,
        const VkMemoryRequirements& memoryRequirements,
        const VkMemoryDedicatedRequirements& dedicatedRequirements,
        u64 offset,
        VulkanDetail::HeapBindingResourceClass::Enum resourceClass,
        const tchar* operationName,
        const tchar* resourceName,
        VulkanDetail::HeapBindingRange& outRange
    )const;
    [[nodiscard]] bool configurePipelineBindings(
        const BindingLayoutVector& bindingLayouts,
        const tchar* operationName,
        PipelineBindingState& outBindings,
        Alloc::ScratchArena& scratchArena
    )const;
    template<typename PipelineT>
    [[nodiscard]] bool configurePipelineBindingsOrDestroy(
        const BindingLayoutVector& bindingLayouts,
        const tchar* operationName,
        PipelineT* pipeline,
        Alloc::ScratchArena& scratchArena
    )const{
        if(configurePipelineBindings(bindingLayouts, operationName, *pipeline, scratchArena))
            return true;

        DestroyArenaObject(m_context.objectArena, pipeline);
        return false;
    }
    template<typename PipelineT>
    [[nodiscard]] bool buildGraphicsPipelineFixedStateOrDestroy(
        const FramebufferInfo& fbinfo,
        const RenderState& renderState,
        const VulkanDetail::PipelineStencilFaceMode::Enum stencilFaceMode,
        const VkDynamicState* dynamicStates,
        const u32 dynamicStateCount,
        const tchar* operationName,
        PipelineT* pipeline,
        VulkanDetail::GraphicsPipelineFixedState& outState
    )const{
        if(VulkanDetail::BuildGraphicsPipelineFixedState(
            fbinfo,
            renderState,
            stencilFaceMode,
            dynamicStates,
            dynamicStateCount,
            operationName,
            outState
        ))
            return true;

        DestroyArenaObject(m_context.objectArena, pipeline);
        return false;
    }
    template<typename PipelineT>
    [[nodiscard]] bool createPipelineOrDestroy(
        const tchar* operationName,
        PipelineT* pipeline,
        const VkComputePipelineCreateInfo& pipelineInfo
    )const{
        const VkResult res = m_context.deviceDispatch.vkCreateComputePipelines(m_context.device, m_context.pipelineCache, 1, &pipelineInfo, m_context.allocationCallbacks, &pipeline->m_pipeline);
        if(res == VK_SUCCESS)
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: {}"), operationName, ResultToString(res));
        DestroyArenaObject(m_context.objectArena, pipeline);
        return false;
    }
    template<typename PipelineT>
    [[nodiscard]] bool createPipelineOrDestroy(
        const tchar* operationName,
        PipelineT* pipeline,
        const VkGraphicsPipelineCreateInfo& pipelineInfo
    )const{
        const VkResult res = m_context.deviceDispatch.vkCreateGraphicsPipelines(m_context.device, m_context.pipelineCache, 1, &pipelineInfo, m_context.allocationCallbacks, &pipeline->m_pipeline);
        if(res == VK_SUCCESS)
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create {}: {}"), operationName, ResultToString(res));
        DestroyArenaObject(m_context.objectArena, pipeline);
        return false;
    }
    void appendPipelineShaderStage(
        Shader* shader,
        VkShaderStageFlagBits stage,
        PipelineSpecializationInfoVector& specializationInfos,
        PipelineShaderStageVector& shaderStages
    )const;


private:
    bool m_gpuCrashDiagnosticsEnabled = false;
    bool m_queueRegistryReady = false;
    u16 m_deviceGeneration = 0u;
    // Actual Vulkan loss and logical quarantine remain distinct so only proven loss may bypass an idle teardown join.
    Atomic<bool> m_deviceLost = false;
    Atomic<bool> m_deviceQuarantined = false;
    Atomic<u64> m_submissionOperationState = 0u;
    Atomic<bool> m_lifecycleDestructionPrepared = false;
    Atomic<bool> m_gpuCrashCaptured = false;
    Atomic<u64> m_nextTimerQueryIncarnation = 0u;
    GpuCrashTracker m_gpuCrashTracker;
    // Pre-reserved crash-capture arena.
    Alloc::PersistentArena m_gpuCrashReportArena;
    Alloc::PersistentArena m_gpuCrashVendorBinaryArena;

    AmdBreadcrumbBuffer m_amdBreadcrumb;

    VulkanContext m_context;
    VulkanAllocator m_allocator;
    DescriptorBufferManager m_descriptorBufferManager;
    GpuDescriptorHeap m_gpuDescriptorHeap;
    Path m_pipelineCacheDirectory;
    Filesystem::FilesystemFactory m_filesystemFactory;
    GraphicsString m_pipelineCacheVolumeName;
    GraphicsVector<NativeQueueState*> m_nativeQueueStates;
    GraphicsVector<Queue*> m_physicalQueues;
    GraphicsVector<GpuPhysicalQueueInfo> m_physicalQueueInfos;
    Array<Queue*, static_cast<u32>(CommandQueue::kCount)> m_primaryQueues = {};
    Array<bool, static_cast<u32>(CommandQueue::kCount)> m_explicitPrimaryQueues = {};
    // Only block-compressed entries are populated; all are resolved before the device is exposed.
    Array<FormatSupport::Mask, static_cast<usize>(Format::kCount)> m_compressedFormatSupport = {};

    UploadManager m_uploadManager;
    UploadManager m_scratchManager;
};


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

