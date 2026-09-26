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


// Command List


class CommandList final : public RefCounter<GraphicsResource>, NoCopy{
    friend class ::NWB::Core::GpuRecordedGraph;
    friend class ::NWB::Core::GpuTimingMeasure;
    friend class ::NWB::Core::GpuTimingSubmissionTicket;
    friend class ::NWB::Core::GpuNativePacketRecorder;
    friend class ::NWB::Core::GpuTaskScheduler;
    friend class Device;
    friend class GpuDescriptorHeap;
    friend class Queue;
    friend class VulkanTestDispatchAccess;


private:
    struct BufferOwnershipRelease{
        BufferRange range;
        GpuPhysicalQueueId destinationQueue;
    };


    struct MarkerStackEntry{
        CommandMarkerRecordingToken token;
        bool usesDebugUtils = false;
        bool usesGpuMarkers = false;
    };


private:
    static constexpr u8 s_GraphPublicationUnowned = 0u;
    static constexpr u8 s_GraphPublicationRecording = 1u;
    static constexpr u8 s_GraphPublicationRecorded = 2u;
    static constexpr u8 s_GraphPublicationSubmitting = 3u;
    static constexpr u8 s_GraphPublicationReading = 4u;
    static constexpr u8 s_GraphPublicationRevoking = 5u;


private:
    class GraphPublicationReadOwnership final : NoCopy{
        friend class CommandList;
        friend class VulkanTestDispatchAccess;


    private:
        explicit GraphPublicationReadOwnership(const CommandList& commandList)noexcept;
        ~GraphPublicationReadOwnership()noexcept;


    private:
        const CommandList& m_commandList;
        bool m_readable = false;
        bool m_acquired = false;
    };

    class GraphRecordingOwnership final : NoCopy{
        friend class ::NWB::Core::GpuNativePacketRecorder;
        friend class CommandList;
        friend class GraphPublicationReadOwnership;
        friend class VulkanTestDispatchAccess;


    private:
        struct Capability{
            const CommandList* commandList = nullptr;
            u64 recordingLeaseSerial = 0u;
            Capability* previous = nullptr;
        };


    private:
        [[nodiscard]] static Capability*& currentCapability()noexcept;
        [[nodiscard]] static bool hasCapability(const CommandList& commandList, u64 recordingLeaseSerial)noexcept;


    private:
        GraphRecordingOwnership(CommandList& commandList, u64 recordingLeaseSerial);
        ~GraphRecordingOwnership()noexcept;


    private:
        [[nodiscard]] bool finish(bool semanticSuccess, CommandListResourceStateHandoff* finalStates);
        void publish()noexcept;
        void release()noexcept;
        void attachCapability()noexcept;
        void detachCapability()noexcept;


    private:
        CommandList& m_commandList;
        u64 m_recordingLeaseSerial = 0u;
        Capability m_capability;
        bool m_acquired = false;
        bool m_capabilityAttached = false;
    };

    class GraphSubmissionOwnership final : NoCopy{
        friend class ::NWB::Core::GpuTaskScheduler;


    public:
        explicit GraphSubmissionOwnership(CommandList& commandList)noexcept;
        ~GraphSubmissionOwnership()noexcept;


    private:
        void accept()noexcept;
        void release()noexcept;


    private:
        CommandList& m_commandList;
        u64 m_recordingLeaseSerial = 0u;
        bool m_acquired = false;
    };


public:
    CommandList(Device& device, const CommandListParameters& params);
    virtual ~CommandList()noexcept override;


public:
    // Optional initial-state handoff; its producer must execute first.
    void open(const CommandListResourceStateHandoff* initialStates = nullptr);
    // Final state snapshot follows keepInitialState restoration.
    void close(CommandListResourceStateHandoff* finalStates = nullptr);
    // False when no submit-ready command buffer is owned.
    [[nodiscard]] bool hasCommandBuffer()const noexcept{
        const GraphPublicationReadOwnership ownership(*this);
        return ownership.m_readable && hasCommandBufferUnchecked();
    }
    // `hasCommandBuffer` remains true after close so queues can submit it. Tooling that emits commands must use
    // this predicate instead of treating ownership as an active recording scope.
    [[nodiscard]] bool isRecording()const noexcept{
        const GraphPublicationReadOwnership ownership(*this);
        return ownership.m_readable && isRecordingUnchecked();
    }
    // Sticky for the open/close attempt: any failed capability or semantic check invalidates the list; close
    // discards its native buffer and only open starts a fresh attempt.
    [[nodiscard]] bool commandRecordingFailed()const noexcept{
        const GraphPublicationReadOwnership ownership(*this);
        return ownership.m_readable && commandRecordingFailedUnchecked();
    }
    // Every open attempt starts a distinct native-buffer lease, including failed attempts. Packet recorders and
    // replay tooling capture this serial before invoking extensible lowering and reject a thunk that closes or
    // replaces the command buffer while claiming success.
    [[nodiscard]] u64 recordingLeaseSerial()const noexcept{
        const GraphPublicationReadOwnership ownership(*this);
        return ownership.m_readable ? recordingLeaseSerialUnchecked() : 0u;
    }
    [[nodiscard]] bool matchesRecordingLease(u64 serial)const noexcept{
        const GraphPublicationReadOwnership ownership(*this);
        return ownership.m_readable && matchesRecordingLeaseUnchecked(serial);
    }
    [[nodiscard]] bool isRenderPassActive()const noexcept{
        const GraphPublicationReadOwnership ownership(*this);
        return ownership.m_readable && m_renderPassActive;
    }
    void clearState();
    void beginRenderPass(Framebuffer& framebuffer, const RenderPassParameters& params);
    void endRenderPass();

    void setResourceStatesForFramebuffer(Framebuffer& framebuffer);
    void commitBarriers();

    void setTextureState(
        Texture* texture,
        TextureSubresourceSet subresources,
        ResourceStates::Mask stateBits,
        bool forceMemoryDependency = false
    );
    void setBufferState(
        Buffer* buffer,
        ResourceStates::Mask stateBits,
        bool forceMemoryDependency = false,
        BufferRange range = s_EntireBuffer
    );
    void setAccelStructState(
        RayTracingAccelStruct* as,
        ResourceStates::Mask stateBits,
        bool forceMemoryDependency = false
    );
    // Exports an exclusive resource to an ordered physical consumer queue.
    void releaseTextureOwnership(Texture* texture, TextureSubresourceSet subresources, CommandQueue::Enum destinationQueue);
    void releaseBufferOwnership(Buffer* buffer, CommandQueue::Enum destinationQueue, BufferRange range = s_EntireBuffer);
    void releaseTextureOwnership(Texture* texture, TextureSubresourceSet subresources, GpuPhysicalQueueId destinationQueue);
    void releaseBufferOwnership(Buffer* buffer, GpuPhysicalQueueId destinationQueue, BufferRange range = s_EntireBuffer);

    void setPermanentTextureState(Texture* texture, ResourceStates::Mask stateBits);
    void setPermanentBufferState(Buffer* buffer, ResourceStates::Mask stateBits);

    void clearTextureFloat(Texture& texture, TextureSubresourceSet subresources, const Color& clearColor);
    void clearTextureRectFloat(Texture& texture, TextureSubresourceSet subresources, const Rect& rect, const Color& clearColor);
    void clearTextureBoxFloat(Texture& texture, TextureSubresourceSet subresources, const Box& box, const Color& clearColor);
    void clearDepthStencilTexture(Texture& texture, TextureSubresourceSet subresources, bool clearDepth, f32 depth, bool clearStencil, u8 stencil);
    void clearDepthStencilTextureRect(Texture& texture, TextureSubresourceSet subresources, const Rect& rect, bool clearDepth, f32 depth, bool clearStencil, u8 stencil);
    void clearDepthStencilTextureBox(Texture& texture, TextureSubresourceSet subresources, const Box& box, bool clearDepth, f32 depth, bool clearStencil, u8 stencil);
    void clearTextureUInt(Texture& texture, TextureSubresourceSet subresources, u32 clearColor);
    void clearTextureUInt(Texture& texture, TextureSubresourceSet subresources, const UIntColor& clearColor);
    void clearTextureRectUInt(Texture& texture, TextureSubresourceSet subresources, const Rect& rect, u32 clearColor);
    void clearTextureRectUInt(Texture& texture, TextureSubresourceSet subresources, const Rect& rect, const UIntColor& clearColor);
    void clearTextureBoxUInt(Texture& texture, TextureSubresourceSet subresources, const Box& box, u32 clearColor);
    void clearTextureBoxUInt(Texture& texture, TextureSubresourceSet subresources, const Box& box, const UIntColor& clearColor);
    void clearTextureInt(Texture& texture, TextureSubresourceSet subresources, i32 clearColor);
    void clearTextureInt(Texture& texture, TextureSubresourceSet subresources, const IntColor& clearColor);
    void clearTextureRectInt(Texture& texture, TextureSubresourceSet subresources, const Rect& rect, i32 clearColor);
    void clearTextureRectInt(Texture& texture, TextureSubresourceSet subresources, const Rect& rect, const IntColor& clearColor);
    void clearTextureBoxInt(Texture& texture, TextureSubresourceSet subresources, const Box& box, i32 clearColor);
    void clearTextureBoxInt(Texture& texture, TextureSubresourceSet subresources, const Box& box, const IntColor& clearColor);

    void copyTexture(Texture& dest, const TextureSlice& destSlice, Texture& src, const TextureSlice& srcSlice);
    void copyTexture(StagingTexture& dest, const TextureSlice& destSlice, Texture& src, const TextureSlice& srcSlice);
    void copyTexture(Texture& dest, const TextureSlice& destSlice, StagingTexture& src, const TextureSlice& srcSlice);
    // Fallible variants let graph recorders reject a packet instead of claiming a lifecycle when staging or preflight fails.
    [[nodiscard]] bool tryWriteBuffer(Buffer& buffer, const void* data, usize dataSize, u64 destOffsetBytes = 0);
    void writeBuffer(Buffer& buffer, const void* data, usize dataSize, u64 destOffsetBytes = 0);
    void clearBufferUInt(Buffer& buffer, u32 clearValue);
    void copyBuffer(Buffer& dest, u64 destOffsetBytes, Buffer& src, u64 srcOffsetBytes, u64 dataSizeBytes);
    // Experimental command-IR hook. The caller has already graph-preflighted the operands and lowered the
    // authoritative CopySource/CopyDest state transitions into this list. This emits only vkCmdCopyBuffer and
    // retains the resources; it intentionally does not mutate CommandList state tracking or synthesize barriers.
    [[nodiscard]] bool recordPreflightedCopyBufferDirectVulkan(
        Buffer& dest,
        u64 destOffsetBytes,
        Buffer& src,
        u64 srcOffsetBytes,
        u64 dataSizeBytes
    );
    [[nodiscard]] bool tryWriteTexture(
        Texture& dest,
        u32 arraySlice,
        u32 mipLevel,
        const void* data,
        usize rowPitch,
        usize depthPitch = 0,
        TextureUploadAspect::Enum aspect = TextureUploadAspect::Automatic
    );
    void writeTexture(
        Texture& dest,
        u32 arraySlice,
        u32 mipLevel,
        const void* data,
        usize rowPitch,
        usize depthPitch = 0,
        TextureUploadAspect::Enum aspect = TextureUploadAspect::Automatic
    );
    void resolveTexture(Texture& dest, const TextureSubresourceSet& dstSubresources, Texture& src, const TextureSubresourceSet& srcSubresources);

    void setPushConstants(const void* data, usize byteSize);

    void setGraphicsState(const GraphicsState& state);
    void draw(const DrawArguments& args);
    void drawIndexed(const DrawArguments& args);
    void drawIndirect(u32 offsetBytes, u32 drawCount = 1);
    void drawIndexedIndirect(u32 offsetBytes, u32 drawCount = 1);

    void setComputeState(const ComputeState& state);
    void dispatch(u32 groupsX, u32 groupsY = 1, u32 groupsZ = 1);
    void dispatchIndirect(u32 offsetBytes);

    void setMeshletState(const MeshletState& state);
    void dispatchMesh(u32 groupsX, u32 groupsY = 1, u32 groupsZ = 1);

    void setRayTracingState(const RayTracingState& state);
    void dispatchRays(const RayTracingDispatchRaysArguments& args);
    void buildBottomLevelAccelStruct(RayTracingAccelStruct* as, const RayTracingGeometryDesc* pGeometries, usize numGeometries, RayTracingAccelStructBuildFlags::Mask buildFlags = RayTracingAccelStructBuildFlags::None);
    void buildTopLevelAccelStruct(RayTracingAccelStruct* as, const RayTracingInstanceDesc* pInstances, usize numInstances, RayTracingAccelStructBuildFlags::Mask buildFlags = RayTracingAccelStructBuildFlags::None);
    void buildOpacityMicromap(RayTracingOpacityMicromap* omm, const RayTracingOpacityMicromapDesc& desc);
    void buildTopLevelAccelStructFromBuffer(RayTracingAccelStruct* as, Buffer* instanceBuffer, u64 instanceBufferOffset, usize numInstances, RayTracingAccelStructBuildFlags::Mask buildFlags = RayTracingAccelStructBuildFlags::None);
    void executeMultiIndirectClusterOperation(const RayTracingClusterOperationDesc& desc);
    void convertCoopVecMatrices(CooperativeVectorConvertMatrixLayoutDesc const* convertDescs, usize numDescs);

    [[nodiscard]] bool resetTimerQuery(TimerQuery& query);
    [[nodiscard]] bool canRecordTimerQueryHere()const;
    [[nodiscard]] bool canResetTimerQueryHere()const;
    [[nodiscard]] bool beginTimerQuery(TimerQuery& query, TimerQueryRecordingToken& outToken);
    [[nodiscard]] bool endTimerQuery(TimerQuery& query, const TimerQueryRecordingToken& token);
    // Claim closure consumes only the beginTimerQuery() claim from this same command buffer; no retention,
    // diagnostics, or observers.
    [[nodiscard]] bool endTimerQueryFromExistingClaim(
        TimerQuery& query,
        const TimerQueryRecordingToken& token
    )noexcept;
    void beginMarker(const AStringView name);
    void endMarker();
    void abandonMarker()noexcept;

#if defined(NWB_DEBUG)
    // Task-graph recording opens one scope around each record thunk. Command methods report the capabilities they
    // actually consume so the packet recorder can reject a declaration that is incompatible with that task.
    void beginTaskCapabilityTracking(GpuQueueCapability::Mask declaredCapabilities);
    [[nodiscard]] GpuQueueCapability::Mask endTaskCapabilityTracking();
    void cancelTaskCapabilityTracking();
#endif

    void setEnableUavBarriersForTexture(Texture* texture, bool enableBarriers);
    void setEnableUavBarriersForBuffer(Buffer* buffer, bool enableBarriers);
    void beginTrackingTextureState(Texture* texture, TextureSubresourceSet subresources, ResourceStates::Mask stateBits);
    void beginTrackingBufferState(Buffer* buffer, ResourceStates::Mask stateBits, BufferRange range = s_EntireBuffer);
    void seedBufferState(Buffer* buffer, ResourceStates::Mask stateBits, BufferRange range = s_EntireBuffer);
    ResourceStates::Mask getTextureSubresourceState(Texture* texture, ArraySlice arraySlice, MipLevel mipLevel);
    ResourceStates::Mask getBufferState(Buffer* buffer, BufferRange range = s_EntireBuffer);
    [[nodiscard]] ResourceStates::Mask getPermanentTextureState(Texture* texture)const;
    [[nodiscard]] ResourceStates::Mask getPermanentBufferState(Buffer* buffer)const;
    [[nodiscard]] bool hasExplicitTextureSubresourceState(Texture* texture, ArraySlice arraySlice, MipLevel mipLevel)const;
    [[nodiscard]] bool hasExplicitBufferState(Buffer* buffer, BufferRange range = s_EntireBuffer, bool requireKnown = false)const;

    Device& getDevice(){ return m_device; }
    const CommandListParameters& getDescription(){ return m_desc; }
    [[nodiscard]] CommandListParameters getResolvedDescription()const noexcept{ return m_creationDesc; }

private:
    [[nodiscard]] bool publicCommandStateAccessible()const noexcept;
    [[nodiscard]] bool hasCommandBufferUnchecked()const noexcept{ return matchesNativeLeaseIdentity(); }
    [[nodiscard]] bool isRecordingUnchecked()const noexcept{ return m_isRecording && matchesNativeLeaseIdentity(); }
    [[nodiscard]] bool commandRecordingFailedUnchecked()const noexcept{ return m_commandRecordingFailed; }
    [[nodiscard]] u64 recordingLeaseSerialUnchecked()const noexcept{ return m_recordingLeaseSerial; }
    [[nodiscard]] bool matchesRecordingLeaseUnchecked(u64 serial)const noexcept{
        return serial != 0u
            && serial == m_recordingLeaseSerial
            && matchesActiveNativeLeaseIdentity()
        ;
    }
    [[nodiscard]] bool canRecordTimerQueryHereUnchecked()const noexcept;
    [[nodiscard]] bool canResetTimerQueryHereUnchecked()const noexcept;
    // False only when graph publication blocks inspection. True reports whether this exact native lease holds
    // the requested cycle's begin and end claims.
    [[nodiscard]] bool inspectExactTimerQueryRecordingEndpoints(
        const TimerQueryRecordingToken& token,
        u64 recordingLeaseSerial,
        bool& outRecordsBegin,
        bool& outRecordsEnd
    )const noexcept;
    [[nodiscard]] bool beginGraphRecordingOwnership(u64 recordingLeaseSerial);
    void publishGraphRecordingOwnership(u64 recordingLeaseSerial)noexcept;
    void cancelGraphRecordingOwnership(u64 recordingLeaseSerial)noexcept;
    // A recorded graph may outlive its strong reference via a task-retained handle. Revoke only that graph's
    // still-unsubmitted publication.
    void revokeGraphRecordingPublication(u64 recordingLeaseSerial)noexcept;
    [[nodiscard]] bool beginGraphSubmissionOwnership(u64& outRecordingLeaseSerial)noexcept;
    void acceptGraphSubmissionOwnership(u64 recordingLeaseSerial)noexcept;
    void endGraphSubmissionOwnership(u64 recordingLeaseSerial)noexcept;
    void closeInternal(CommandListResourceStateHandoff* finalStates);
    // Reusable attempt cancellation leaves publication and crash-tracker ownership to the enclosing capability.
    // It never logs, invokes observers, polls a queue, or archives crash-marker strings.
    void abortRecordingAttemptWithoutCallbacks()noexcept;
    void clearStateInternal();
    [[nodiscard]] bool descriptionMatchesCreation()const noexcept;
    [[nodiscard]] bool matchesNativeLeaseIdentity()const noexcept;
    [[nodiscard]] bool matchesActiveNativeLeaseIdentity()const noexcept;
    [[nodiscard]] bool matchesSubmissionLease(
        GpuPhysicalQueueId executionQueue,
        CommandQueue::Enum executionQueueClass,
        bool graphSubmissionAuthorized
    )const noexcept;
    [[nodiscard]] bool validateFramebufferForRendering(Framebuffer* framebuffer, const tchar* operationName);
    [[nodiscard]] bool validateRenderPassBegin(
        Framebuffer& framebuffer,
        const RenderPassParameters& params,
        const tchar* operationName
    );
    [[nodiscard]] bool prepareFramebufferForRendering(Framebuffer* framebuffer, const tchar* operationName);
    [[nodiscard]] bool validateViewportState(
        const ViewportState& viewport,
        const tchar* operationName
    );
    [[nodiscard]] bool validateTextureForGpuState(
        Texture* texture,
        ResourceStates::Mask requiredState,
        const tchar* operationName,
        VkImageUsageFlags requiredUsage = 0u
    );
    [[nodiscard]] bool validateBufferForGpuState(
        Buffer* buffer,
        ResourceStates::Mask requiredState,
        const tchar* operationName,
        VkBufferUsageFlags explicitRequiredUsage = 0u
    );
    [[nodiscard]] bool validateGraphicsState(const GraphicsState& state);
    [[nodiscard]] bool validateMeshletState(const MeshletState& state);
    [[nodiscard]] bool validateGraphicsDrawState(const tchar* operationName, bool indexed);
    [[nodiscard]] bool validateGraphicsDrawArguments(
        const DrawArguments& arguments,
        bool indexed,
        const tchar* operationName
    );
    void setResourceStatesForGraphicsBuffers(const GraphicsState& state);
    [[nodiscard]] bool isTextureAdmittedToCommandQueue(const Texture& texture)const noexcept;
    [[nodiscard]] bool isTextureReadyForCommandQueue(
        Texture* texture,
        VkImageUsageFlags requiredUsage = 0u
    )const noexcept;
    [[nodiscard]] bool isBufferAdmittedToCommandQueue(const Buffer& buffer)const noexcept;
    [[nodiscard]] bool isBufferReadyForCommandQueue(
        Buffer* buffer,
        VkBufferUsageFlags requiredUsage = 0u
    )const noexcept;
    [[nodiscard]] bool validateTrackedTexturesReadyForClose();
    [[nodiscard]] bool validateTrackedBuffersReadyForClose();
    [[nodiscard]] bool validateTrackedResourcesReadyForSubmission()const;
    [[nodiscard]] bool importResourceStateHandoff(const CommandListResourceStateHandoff& states);
    void exportResourceStateHandoff(CommandListResourceStateHandoff& states)const;
    void appendPendingOwnershipReleaseBarriers();
    void collectHostReadbackBuffers();
    void appendHostReadbackBarriers();
    void registerHostReadbackBuffer(Buffer& buffer);
    void registerHostReadbackStagingTexture(StagingTexture& stagingTexture);
    void retainResource(Buffer* resource);
    void retainResource(Texture* resource);
    void retainResource(Framebuffer* resource);
    void retainResource(GraphicsResource* resource);
    void retainStagingBuffer(Buffer& buffer);
    void bindDescriptorBufferHeap(
        GpuDescriptorHeap& heap,
        const ComputePipeline& pipeline,
        GpuDescriptorHandle accelStructHandle
    );
    void bindDescriptorBufferHeap(GpuDescriptorHeap& heap, const GraphicsPipeline& pipeline);
    void bindDescriptorBufferHeap(GpuDescriptorHeap& heap, const MeshletPipeline& pipeline);
    void bindDescriptorBufferHeap(
        GpuDescriptorHeap& heap,
        const RayTracingPipeline& pipeline,
        GpuDescriptorHandle accelStructHandle
    );
    void bindDescriptorBufferHeapNative(
        GpuDescriptorHeap& heap,
        VkPipelineBindPoint bindPoint,
        const PipelineBindingState& pipelineBindings,
        GpuDescriptorHandle accelStructHandle,
        const tchar* operationName
    );
    void ensureDescriptorBuffersBound(
        DescriptorBufferManager& manager,
        const DescriptorBufferManager::BindingSnapshot& snapshot
    );
    void setViewportState(const ViewportState& viewport);

    bool beginDynamicRendering(Framebuffer& framebuffer, const RenderPassParameters& params);
    void endDynamicRendering();
    bool ensureGraphicsRenderPass(Framebuffer* framebuffer);
    void endActiveRenderPass();
    void executePipelineBarrier(const VkDependencyInfo& depInfo);
    [[nodiscard]] bool validateCommandRecordingScope(const tchar* operationName);
    [[nodiscard]] bool recordAndValidateCommandCapability(GpuQueueCapability::Mask requiredCapabilities, const tchar* operationName);
    [[nodiscard]] bool recordAndValidateAnyCommandCapability(GpuQueueCapability::Mask alternativeCapabilities, const tchar* operationName);
    void rejectCommandRecording(const tchar* operationName, const tchar* reason);
    void invalidateCommandRecording()noexcept;
    void discardInvalidCommandBuffer();
    [[nodiscard]] bool validateIndirectBuffer(
        Buffer* buffer,
        u64 offsetBytes,
        u64 commandSizeBytes,
        u32 commandCount,
        const tchar* commandName
    );
    [[nodiscard]] bool prepareDrawIndirect(
        u32 offsetBytes,
        u32 drawCount,
        u64 commandSizeBytes,
        const tchar* operationLabel,
        const tchar* commandName,
        VulkanDetail::IndirectDrawIndexMode::Enum indexMode,
        Buffer*& outIndirectBuffer
    );
    void clearColorTexture(Texture& texture, TextureSubresourceSet subresources, const tchar* valueName, const VkClearColorValue& clearValue, bool integerValue, bool signedIntegerValue);
    void clearColorTextureBox(Texture& texture, TextureSubresourceSet subresources, const Box& box, const tchar* valueName, const VkClearColorValue& clearValue, bool integerValue, bool signedIntegerValue);
    bool clearActiveRenderPassColorTextureRect(Texture& texture, const TextureSubresourceSet& resolvedSubresources, const Rect& rect, const VkClearColorValue& clearValue, const tchar* valueName);
    bool clearActiveRenderPassDepthStencilTextureRect(Texture& texture, const TextureSubresourceSet& resolvedSubresources, const Rect& rect, bool clearDepth, f32 depth, bool clearStencil, u8 stencil);
    [[nodiscard]] bool validateStagingTextureCopyResources(
        StagingTexture& stagingTexture,
        Texture& texture,
        CpuAccessMode::Enum requiredCpuAccess,
        VkImageUsageFlags requiredImageUsage,
        const tchar* operationName
    );
    bool prepareStagingTextureCopy(
        StagingTexture& stagingResource,
        const TextureSlice& stagingSlice,
        Texture& textureResource,
        const TextureSlice& textureSlice,
        VkBufferImageCopy& outRegion
    )const;
    bool prepareUploadStaging(
        usize dataSize,
        const tchar* operationName,
        Buffer*& outStagingBuffer,
        u64& outStagingOffset,
        void*& outCpuVA,
        u32 alignment = s_DefaultUploadSuballocationAlignment
    );
    bool prepareUploadStaging(
        const void* data,
        usize dataSize,
        const tchar* operationName,
        Buffer*& outStagingBuffer,
        u64& outStagingOffset,
        u32 alignment = s_DefaultUploadSuballocationAlignment
    );
    [[nodiscard]] bool suballocateBuildScratchAddress(
        u64 buildScratchSize,
        u64 scratchAlignment,
        VkDeviceAddress& outScratchAddress,
        const tchar* operationName
    );
    [[nodiscard]] bool validateAccelStructBuildSignature(
        AccelStruct& accelStruct,
        VkAccelerationStructureTypeKHR accelStructType,
        VkBuildAccelerationStructureFlagsKHR buildFlags,
        const AccelStructGeometryBuildSignature* geometrySignatures,
        usize geometrySignatureCount,
        bool performUpdate,
        const tchar* operationName,
        bool& outHasPriorBuild
    );
    bool buildTopLevelAccelStructFromInstanceData(
        RayTracingAccelStruct& as,
        VkDeviceAddress instanceDataAddress,
        usize numInstances,
        RayTracingAccelStructBuildFlags::Mask buildFlags,
        VkBuildAccelerationStructureFlagsKHR vkBuildFlags,
        const tchar* operationName
    );
    [[nodiscard]] CommandMarkerRecordingToken beginMarkerLease(const AStringView name);
    [[nodiscard]] bool endMarkerLease(const CommandMarkerRecordingToken& token);
    void abandonMarkerLease(const CommandMarkerRecordingToken& token)noexcept;
    [[nodiscard]] bool markerLeaseMatchesTop(const CommandMarkerRecordingToken& token)const noexcept;
    void closeTopMarkerWithoutCallbacks()noexcept;
    void resetMarkerState();
    void resetMarkerStateWithoutCallbacks()noexcept;
    void discardUnsubmittedUploadChunks();
    void abandonUnsubmittedUploadChunks()noexcept;


private:
    const CommandListParameters m_creationDesc;
    CommandListParameters m_desc;
    TrackedCommandBufferPtr m_currentCmdBuf;
    StateTracker m_stateTracker;
    VulkanDetail::HostReadbackBarrierTracker m_hostReadbackBarrierTracker;
    u64 m_recordingLeaseSerial = 0u;
    Atomic<u64> m_graphRecordingOwnershipSerial{ 0u };
    mutable Atomic<u8> m_graphPublicationState{ s_GraphPublicationUnowned };
    u64 m_nativeRecordingID = 0u;
    u64 m_nextMarkerSerial = 0u;
    GraphicsVector<MarkerStackEntry> m_markerStack;
    Framebuffer* m_renderPassFramebuffer = nullptr;
    bool m_enableAutomaticBarriers = true;
    bool m_isRecording = false;
    bool m_commandRecordingFailed = false;
    bool m_renderPassActive = false;
    bool m_descriptorBuffersBound = false;
#if defined(NWB_DEBUG)
    GpuQueueCapability::Mask m_taskCapabilitiesUsed = GpuQueueCapability::None;
    GpuQueueCapability::Mask m_taskDeclaredCapabilities = GpuQueueCapability::None;
    bool m_taskCapabilityTracking = false;
#endif

    GraphicsState m_currentGraphicsState;
    ComputeState m_currentComputeState;
    MeshletState m_currentMeshletState;
    RayTracingState m_currentRayTracingState;

    Device& m_device;
    const VulkanContext& m_context;
    GpuCrashMarkerTracker m_gpuCrashMarkerTracker;

    Vector<VkImageMemoryBarrier2, Alloc::GlobalArena> m_pendingImageBarriers;
    Vector<VkBufferMemoryBarrier2, Alloc::GlobalArena> m_pendingBufferBarriers;
    HashMap<TextureSubresourceStateKey, GpuPhysicalQueueId, TextureSubresourceStateKeyHasher, TextureSubresourceStateKeyEqualTo, Alloc::GlobalArena> m_textureOwnershipReleaseDestinations;
    HashMap<Buffer*, Vector<BufferOwnershipRelease, Alloc::GlobalArena>, Hasher<Buffer*>, EqualTo<Buffer*>, Alloc::GlobalArena> m_bufferOwnershipReleaseDestinations;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Event Query


class EventQuery final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;


public:
    EventQuery(const VulkanContext& context);
    ~EventQuery();


private:
    Futex m_mutex;
    VkFence m_fence = VK_NULL_HANDLE;

    const VulkanContext& m_context;
    bool m_started = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Timer Query


class TimerQuery final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;
    friend class CommandList;
    friend class TrackedCommandBuffer;


private:
    struct RecordingOwner{
        TrackedCommandBuffer* commandBuffer = nullptr;
        u64 recordingID = 0u;
    };


public:
    TimerQuery(const VulkanContext& context, u64 incarnation);
    ~TimerQuery();


public:
    // The caller owns this unaccepted recording transaction; its command buffers cannot submit concurrently.
    [[nodiscard]] bool discardUnacceptedRecording(const TimerQueryRecordingToken& token)noexcept;
    // The caller has made its prior endpoint packet permanently non-submittable. Preserve the accepted begin cycle
    // while relinquishing only the stale end owner so a recovery command buffer can record its replacement endpoint.
    [[nodiscard]] bool releaseUnacceptedEndForRecovery(const TimerQueryRecordingToken& token)noexcept;


private:
    Futex m_mutex;
    VkQueryPool m_queryPool = VK_NULL_HANDLE;
    GpuPhysicalQueueId m_timestampQueue;
    GpuPhysicalQueueId m_cycleBaselineQueue;
    GpuPhysicalQueueId m_cycleQueue;
    RecordingOwner m_resetRecordingOwner;
    RecordingOwner m_beginRecordingOwner;
    RecordingOwner m_endRecordingOwner;
    u64 m_incarnation = 0u;
    u64 m_nextRecordingGeneration = 0u;
    u64 m_nextResetAuthorizationGeneration = 0u;
    u64 m_lastAcceptedRecordingGeneration = 0u;
    u64 m_resetRecordingAuthorizationGeneration = 0u;
    u64 m_cycleGeneration = 0u;
    u64 m_cycleBaselineCompletionGeneration = 0u;
    u64 m_completedCycleGeneration = 0u;
    u64 m_resetAuthorizationGeneration = 0u;
    QueueSubmissionToken m_cycleBaselineCompletion;
    QueueSubmissionToken m_completedCycleSubmission;
    QueueSubmissionToken m_resetAuthorizationSubmission;
    u32 m_timestampValidBits = 0u;
    u32 m_cycleBaselineValidBits = 0u;
    u32 m_cycleValidBits = 0u;
    bool m_cycleBaselineActive = false;
    bool m_beginAccepted = false;
    bool m_cycleInvalidated = false;
    bool m_resetAuthorizationAvailable = false;
    bool m_recordingActive = false;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

