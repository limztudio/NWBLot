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

#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Command operation labels shared by command-list recording paths.
inline constexpr TStringView s_CloseCommandListOperation = NWB_TEXT("close command list");
inline constexpr TStringView s_OwnershipReleaseBarriersOperation = NWB_TEXT("append ownership-release barriers");
inline constexpr TStringView s_DispatchRaysOperation = NWB_TEXT("dispatch rays");
inline constexpr TStringView s_CopyBufferOperation = NWB_TEXT("copy buffer");
inline constexpr TStringView s_ClearTextureBoxOperation = NWB_TEXT("clear texture box");
inline constexpr TStringView s_ReleaseTextureOwnershipOperation = NWB_TEXT("release texture ownership");
inline constexpr TStringView s_SetPushConstantsOperation = NWB_TEXT("set push constants");
inline constexpr TStringView s_SetGraphicsStateOperation = NWB_TEXT("set graphics state");
inline constexpr TStringView s_SetMeshletStateOperation = NWB_TEXT("set meshlet state");

// Rejection reasons shared by command-list recording paths.
inline constexpr TStringView s_DataIsNullReason = NWB_TEXT("data is null");
inline constexpr TStringView s_StagingAllocationFailedReason = NWB_TEXT("staging allocation failed");
inline constexpr TStringView s_NativeBufferAliasReason = NWB_TEXT("distinct buffer objects alias the same native buffer");
inline constexpr TStringView s_TextureNotReadyForQueueReason = NWB_TEXT("texture is not ready for this exact command queue");
inline constexpr TStringView s_BufferNotReadyForQueueReason = NWB_TEXT("buffer is not ready for this exact command queue");
inline constexpr TStringView s_MeshShaderUnavailableReason = NWB_TEXT("mesh shader feature or entry point is unavailable");

// Index buffer and indirect-argument buffer states tracked next to the vertex buffers.
inline constexpr u32 s_GraphicsExtraBufferStateCount = 2u;


class CommandList final : public RefCounter<GraphicsResource>, NoCopy{
    friend class ::NWB::Core::GpuRecordedGraph;
    friend class ::NWB::Core::GpuTimingMeasure;
    friend class ::NWB::Core::GpuTimingSubmissionTicket;
    friend class ::NWB::Core::GpuNativePacketRecorder;
    friend class ::NWB::Core::GpuTaskScheduler;
    friend class Device;
    friend class GpuDescriptorHeap;
    friend class Queue;


private:
    struct BufferOwnershipRelease{
        BufferRange range;
        GpuPhysicalQueueId destinationQueue;
    };


    struct MarkerStackEntry{
        CommandMarkerRecordingToken token;
        b8 usesDebugUtils = false;
        b8 usesGpuMarkers = false;
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


    private:
        explicit GraphPublicationReadOwnership(const CommandList& commandList)noexcept;
        ~GraphPublicationReadOwnership()noexcept;


    private:
        const CommandList& m_commandList;
        b8 m_readable = false;
        b8 m_acquired = false;
    };

    class GraphRecordingOwnership final : NoCopy{
        friend class ::NWB::Core::GpuNativePacketRecorder;
        friend class CommandList;
        friend class GraphPublicationReadOwnership;


    private:
        struct Capability{
            const CommandList* commandList = nullptr;
            u64 recordingLeaseSerial = 0u;
            Capability* previous = nullptr;
        };


    private:
        [[nodiscard]] static Capability*& CurrentCapability()noexcept;
        [[nodiscard]] static b8 HasCapability(const CommandList& commandList, u64 recordingLeaseSerial)noexcept;


    private:
        GraphRecordingOwnership(CommandList& commandList, u64 recordingLeaseSerial);
        ~GraphRecordingOwnership()noexcept;


    private:
        [[nodiscard]] b8 finish(b8 semanticSuccess, CommandListResourceStateHandoff* finalStates);
        void publish()noexcept;
        void release()noexcept;
        void attachCapability()noexcept;
        void detachCapability()noexcept;


    private:
        CommandList& m_commandList;
        u64 m_recordingLeaseSerial = 0u;
        Capability m_capability;
        b8 m_acquired = false;
        b8 m_capabilityAttached = false;
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
        b8 m_acquired = false;
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
    [[nodiscard]] b8 hasCommandBuffer()const noexcept{
        const GraphPublicationReadOwnership ownership(*this);
        return ownership.m_readable && hasCommandBufferUnchecked();
    }
    // `hasCommandBuffer` remains true after close so queues can submit it.
    // Tooling that emits commands must use this predicate instead of treating ownership as an active recording scope.
    [[nodiscard]] b8 isRecording()const noexcept{
        const GraphPublicationReadOwnership ownership(*this);
        return ownership.m_readable && isRecordingUnchecked();
    }
    // Sticky per attempt: any failed check invalidates the list; close drops its buffer, only open starts fresh.
    [[nodiscard]] b8 commandRecordingFailed()const noexcept{
        const GraphPublicationReadOwnership ownership(*this);
        return ownership.m_readable && commandRecordingFailedUnchecked();
    }
    // Every open starts a distinct lease. Recorders capture it before lowering; a thunk that swaps the buffer while claiming success is rejected.
    [[nodiscard]] u64 recordingLeaseSerial()const noexcept{
        const GraphPublicationReadOwnership ownership(*this);
        return ownership.m_readable ? recordingLeaseSerialUnchecked() : 0u;
    }

    [[nodiscard]] b8 matchesRecordingLease(u64 serial)const noexcept{
        const GraphPublicationReadOwnership ownership(*this);
        return ownership.m_readable && matchesRecordingLeaseUnchecked(serial);
    }

    [[nodiscard]] b8 isRenderPassActive()const noexcept{
        const GraphPublicationReadOwnership ownership(*this);
        return ownership.m_readable && m_renderPassActive;
    }
    void clearState();
    void beginRenderPass(Framebuffer& framebuffer, const RenderPassParameters& params);
    void endRenderPass();

    void setResourceStatesForFramebuffer(Framebuffer& framebuffer);
    void commitBarriers();

    void setTextureState(Texture* textureResource, TextureSubresourceSet subresources, ResourceStates::Mask stateBits, b8 forceMemoryDependency = false);
    void setBufferState(Buffer* bufferResource, ResourceStates::Mask stateBits, b8 forceMemoryDependency = false, BufferRange range = s_EntireBuffer);
    void setAccelStructState(RayTracingAccelStruct* accelStructResource, ResourceStates::Mask stateBits, b8 forceMemoryDependency = false);
    // Exports an exclusive resource to an ordered physical consumer queue.
    void releaseTextureOwnership(Texture* textureResource, TextureSubresourceSet subresources, CommandQueue::Enum destinationQueue);
    void releaseBufferOwnership(Buffer* bufferResource, CommandQueue::Enum destinationQueue, BufferRange range = s_EntireBuffer);
    void releaseTextureOwnership(Texture* textureResource, TextureSubresourceSet subresources, GpuPhysicalQueueId destinationQueue);
    void releaseBufferOwnership(Buffer* bufferResource, GpuPhysicalQueueId destinationQueue, BufferRange range = s_EntireBuffer);

    void setPermanentTextureState(Texture* texture, ResourceStates::Mask stateBits);
    void setPermanentBufferState(Buffer* buffer, ResourceStates::Mask stateBits);

    void clearTextureFloat(Texture& texture, TextureSubresourceSet subresources, const Color& clearColor);
    void clearTextureRectFloat(Texture& texture, TextureSubresourceSet subresources, const Rect& rect, const Color& clearColor);
    void clearTextureBoxFloat(Texture& texture, TextureSubresourceSet subresources, const Box& box, const Color& clearColor);
    void clearDepthStencilTexture(Texture& texture, TextureSubresourceSet subresources, b8 clearDepth, f32 depth, b8 clearStencil, u8 stencil);
    void clearDepthStencilTextureRect(Texture& texture, TextureSubresourceSet subresources, const Rect& rect, b8 clearDepth, f32 depth, b8 clearStencil, u8 stencil);
    void clearDepthStencilTextureBox(Texture& texture, TextureSubresourceSet subresources, const Box& box, b8 clearDepth, f32 depth, b8 clearStencil, u8 stencil);
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
    [[nodiscard]] b8 tryWriteBuffer(Buffer& buffer, const void* data, usize dataSize, u64 destOffsetBytes = 0);
    void writeBuffer(Buffer& buffer, const void* data, usize dataSize, u64 destOffsetBytes = 0);
    void clearBufferUInt(Buffer& buffer, u32 clearValue);
    void copyBuffer(Buffer& dest, u64 destOffsetBytes, Buffer& src, u64 srcOffsetBytes, u64 dataSizeBytes);
    [[nodiscard]] b8 tryWriteTexture(Texture& dest, u32 arraySlice, u32 mipLevel, const void* data, usize rowPitch, usize depthPitch = 0, TextureUploadAspect::Enum aspect = TextureUploadAspect::Automatic);
    void writeTexture(Texture& dest, u32 arraySlice, u32 mipLevel, const void* data, usize rowPitch, usize depthPitch = 0, TextureUploadAspect::Enum aspect = TextureUploadAspect::Automatic);
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
    void buildBottomLevelAccelStruct(RayTracingAccelStruct* accelStructResource, const RayTracingGeometryDesc* geometries, usize numGeometries, RayTracingAccelStructBuildFlags::Mask buildFlags = RayTracingAccelStructBuildFlags::None);
    void buildTopLevelAccelStruct(RayTracingAccelStruct* accelStructResource, const RayTracingInstanceDesc* instances, usize numInstances, RayTracingAccelStructBuildFlags::Mask buildFlags = RayTracingAccelStructBuildFlags::None);
    void buildOpacityMicromap(RayTracingOpacityMicromap* opacityMicromapResource, const RayTracingOpacityMicromapDesc& ommDesc);
    void buildTopLevelAccelStructFromBuffer(RayTracingAccelStruct* accelStructResource, Buffer* instanceBuffer, u64 instanceBufferOffset, usize numInstances, RayTracingAccelStructBuildFlags::Mask buildFlags = RayTracingAccelStructBuildFlags::None);
    void executeMultiIndirectClusterOperation(const RayTracingClusterOperationDesc& opDesc);
    void convertCoopVecMatrices(CooperativeVectorConvertMatrixLayoutDesc const* convertDescs, usize numDescs);

    [[nodiscard]] b8 resetTimerQuery(TimerQuery& query);
    [[nodiscard]] b8 canRecordTimerQueryHere()const noexcept;
    [[nodiscard]] b8 canResetTimerQueryHere()const noexcept;
    [[nodiscard]] Expected<TimerQueryRecordingToken> beginTimerQuery(TimerQuery& query);
    [[nodiscard]] b8 endTimerQuery(TimerQuery& query, const TimerQueryRecordingToken& token);
    // Claim closure consumes only the beginTimerQuery() claim from this same command buffer; no retention, diagnostics, or observers.
    [[nodiscard]] b8 endTimerQueryFromExistingClaim(TimerQuery& query, const TimerQueryRecordingToken& token)noexcept;
    void beginMarker(const AStringView name);
    void endMarker();
    void abandonMarker()noexcept;

#if defined(NWB_DEBUG)
    // Task-graph recording opens one scope around each record thunk.
    // Command methods report the capabilities they actually consume so the packet recorder can reject a declaration that is incompatible with that task.
    void beginTaskCapabilityTracking(GpuQueueCapability::Mask declaredCapabilities)noexcept;
    [[nodiscard]] GpuQueueCapability::Mask endTaskCapabilityTracking()noexcept;
    void cancelTaskCapabilityTracking()noexcept;
#endif

    void setEnableUavBarriersForTexture(Texture* texture, b8 enableBarriers);
    void setEnableUavBarriersForBuffer(Buffer* buffer, b8 enableBarriers);
    void beginTrackingTextureState(Texture* texture, TextureSubresourceSet subresources, ResourceStates::Mask stateBits);
    void beginTrackingBufferState(Buffer* buffer, ResourceStates::Mask stateBits, BufferRange range = s_EntireBuffer);
    void seedBufferState(Buffer* buffer, ResourceStates::Mask stateBits, BufferRange range = s_EntireBuffer);
    ResourceStates::Mask getTextureSubresourceState(Texture* texture, ArraySlice arraySlice, MipLevel mipLevel)noexcept;
    ResourceStates::Mask getBufferState(Buffer* buffer, BufferRange range = s_EntireBuffer)noexcept;
    [[nodiscard]] ResourceStates::Mask getPermanentTextureState(Texture* texture)const noexcept;
    [[nodiscard]] ResourceStates::Mask getPermanentBufferState(Buffer* buffer)const noexcept;
    [[nodiscard]] b8 hasExplicitTextureSubresourceState(Texture* texture, ArraySlice arraySlice, MipLevel mipLevel)const noexcept;
    [[nodiscard]] b8 hasExplicitBufferState(Buffer* buffer, BufferRange range = s_EntireBuffer, b8 requireKnown = false)const noexcept;

    Device& getDevice()noexcept{ return m_device; }

    const CommandListParameters& getDescription()noexcept{ return m_desc; }

    [[nodiscard]] CommandListParameters getResolvedDescription()const noexcept{ return m_creationDesc; }

private:
    [[nodiscard]] b8 publicCommandStateAccessible()const noexcept;
    [[nodiscard]] b8 hasCommandBufferUnchecked()const noexcept{ return matchesNativeLeaseIdentity(); }

    [[nodiscard]] b8 isRecordingUnchecked()const noexcept{ return m_isRecording && matchesNativeLeaseIdentity(); }

    [[nodiscard]] b8 commandRecordingFailedUnchecked()const noexcept{ return m_commandRecordingFailed; }

    [[nodiscard]] u64 recordingLeaseSerialUnchecked()const noexcept{ return m_recordingLeaseSerial; }

    [[nodiscard]] b8 matchesRecordingLeaseUnchecked(u64 serial)const noexcept{
        return serial != 0u && serial == m_recordingLeaseSerial && matchesActiveNativeLeaseIdentity();
    }
    [[nodiscard]] b8 canRecordTimerQueryHereUnchecked()const noexcept;
    [[nodiscard]] b8 canResetTimerQueryHereUnchecked()const noexcept;
    // False only when graph publication blocks inspection.
    // True reports whether this exact native lease holds the requested cycle's begin and end claims.
    [[nodiscard]] Expected<TimerQueryRecordingEndpoints> inspectExactTimerQueryRecordingEndpoints(const TimerQueryRecordingToken& token, u64 recordingLeaseSerial)const noexcept;
    [[nodiscard]] b8 beginGraphRecordingOwnership(u64 recordingLeaseSerial);
    void publishGraphRecordingOwnership(u64 recordingLeaseSerial)noexcept;
    void cancelGraphRecordingOwnership(u64 recordingLeaseSerial)noexcept;
    // A recorded graph may outlive its strong reference via a task-retained handle.
    // Revoke only that graph's still-unsubmitted publication.
    void revokeGraphRecordingPublication(u64 recordingLeaseSerial)noexcept;
    [[nodiscard]] Expected<u64> beginGraphSubmissionOwnership()noexcept;
    void acceptGraphSubmissionOwnership(u64 recordingLeaseSerial)noexcept;
    void endGraphSubmissionOwnership(u64 recordingLeaseSerial)noexcept;
    void closeInternal(CommandListResourceStateHandoff* finalStates);
    // Reusable attempt cancellation leaves publication and crash-tracker ownership to the enclosing capability.
    // It never logs, invokes observers, polls a queue, or archives crash-marker strings.
    void abortRecordingAttemptWithoutCallbacks()noexcept;
    void clearStateInternal();
    [[nodiscard]] b8 descriptionMatchesCreation()const noexcept;
    [[nodiscard]] b8 matchesNativeLeaseIdentity()const noexcept;
    [[nodiscard]] b8 matchesActiveNativeLeaseIdentity()const noexcept;
    [[nodiscard]] b8 matchesSubmissionLease(GpuPhysicalQueueId executionQueue, CommandQueue::Enum executionQueueClass, b8 graphSubmissionAuthorized)const noexcept;
    [[nodiscard]] b8 validateFramebufferForRendering(Framebuffer* framebuffer, TStringView operationName);
    [[nodiscard]] b8 validateRenderPassBegin(Framebuffer& framebuffer, const RenderPassParameters& params, TStringView operationName);
    [[nodiscard]] b8 prepareFramebufferForRendering(Framebuffer* framebuffer, TStringView operationName);
    [[nodiscard]] b8 validateViewportState(const ViewportState& viewportState, TStringView operationName);
    [[nodiscard]] b8 validateTextureForGpuState(Texture* texture, ResourceStates::Mask requiredState, TStringView operationName, VkImageUsageFlags explicitRequiredUsage = 0u);
    [[nodiscard]] b8 validateBufferForGpuState(Buffer* buffer, ResourceStates::Mask requiredState, TStringView operationName, VkBufferUsageFlags explicitRequiredUsage = 0u);
    [[nodiscard]] b8 validateGraphicsState(const GraphicsState& state);
    [[nodiscard]] b8 validateMeshletState(const MeshletState& state);
    [[nodiscard]] b8 validateGraphicsDrawState(TStringView operationName, b8 indexed);
    [[nodiscard]] b8 validateGraphicsDrawArguments(const DrawArguments& arguments, b8 indexed, TStringView operationName);
    void setResourceStatesForGraphicsBuffers(const GraphicsState& state);
    [[nodiscard]] b8 isTextureAdmittedToCommandQueue(const Texture& texture)const noexcept;
    [[nodiscard]] b8 isTextureReadyForCommandQueue(Texture* texture, VkImageUsageFlags requiredUsage = 0u)const noexcept;
    [[nodiscard]] b8 isBufferAdmittedToCommandQueue(const Buffer& buffer)const noexcept;
    [[nodiscard]] b8 isBufferReadyForCommandQueue(Buffer* buffer, VkBufferUsageFlags requiredUsage = 0u)const noexcept;
    [[nodiscard]] b8 validateTrackedTexturesReadyForClose();
    [[nodiscard]] b8 validateTrackedBuffersReadyForClose();
    [[nodiscard]] b8 validateTrackedResourcesReadyForSubmission()const;
    [[nodiscard]] b8 importResourceStateHandoff(Alloc::ScratchArena& scratchArena, const CommandListResourceStateHandoff& states);
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
    void bindDescriptorBufferHeap(GpuDescriptorHeap& heap, const ComputePipeline& pipeline, GpuDescriptorHandle accelStructHandle);
    void bindDescriptorBufferHeap(GpuDescriptorHeap& heap, const GraphicsPipeline& pipeline);
    void bindDescriptorBufferHeap(GpuDescriptorHeap& heap, const MeshletPipeline& pipeline);
    void bindDescriptorBufferHeap(GpuDescriptorHeap& heap, const RayTracingPipeline& pipeline, GpuDescriptorHandle accelStructHandle);
    void bindDescriptorBufferHeapNative(GpuDescriptorHeap& heap, VkPipelineBindPoint bindPoint, const PipelineBindingState& pipelineBindings, GpuDescriptorHandle accelStructHandle, TStringView operationName);
    void ensureDescriptorBuffersBound(DescriptorBufferManager& manager, const DescriptorBufferManager::BindingSnapshot& snapshot);
    void setViewportState(const ViewportState& viewportState);

    b8 beginDynamicRendering(Framebuffer& framebuffer, const RenderPassParameters& params);
    void endDynamicRendering();
    b8 ensureGraphicsRenderPass(Framebuffer* framebuffer);
    void endActiveRenderPass();
    void executePipelineBarrier(Span<VkMemoryBarrier2> memoryBarriers, Span<VkImageMemoryBarrier2> imageBarriers, Span<VkBufferMemoryBarrier2> bufferBarriers);
    [[nodiscard]] b8 validateCommandRecordingScope(TStringView operationName);
    [[nodiscard]] b8 recordAndValidateCommandCapability(GpuQueueCapability::Mask requiredCapabilities, TStringView operationName);
    [[nodiscard]] b8 recordAndValidateAnyCommandCapability(GpuQueueCapability::Mask alternativeCapabilities, TStringView operationName);
    void rejectCommandRecording(TStringView operationName, TStringView reason);
    void invalidateCommandRecording()noexcept;
    void discardInvalidCommandBuffer();
    [[nodiscard]] b8 validateIndirectBuffer(Buffer* buffer, u64 offsetBytes, u64 commandSizeBytes, u32 commandCount, TStringView commandName);
    [[nodiscard]] Expected<NotNull<Buffer*>> prepareDrawIndirect(u32 offsetBytes, u32 drawCount, u64 commandSizeBytes, TStringView operationLabel, TStringView commandName, VulkanDetail::IndirectDrawIndexMode::Enum indexMode);
    void clearColorTexture(Alloc::ScratchArena& scratchArena, Texture& texture, TextureSubresourceSet subresources, const VkClearColorValue& clearValue, b8 integerValue, b8 signedIntegerValue);
    void clearColorTextureBox(Alloc::ScratchArena& scratchArena, Texture& texture, TextureSubresourceSet subresources, const Box& box, const VkClearColorValue& clearValue, b8 integerValue, b8 signedIntegerValue);
    b8 clearActiveRenderPassColorTextureRect(Texture& texture, const TextureSubresourceSet& resolvedSubresources, const Rect& rect, const VkClearColorValue& clearValue);
    b8 clearActiveRenderPassDepthStencilTextureRect(Texture& texture, const TextureSubresourceSet& resolvedSubresources, const Rect& rect, b8 clearDepth, f32 depth, b8 clearStencil, u8 stencil);
    [[nodiscard]] b8 validateTextureTransferResources(Texture& dest, ResourceStates::Mask destState, Texture& src, ResourceStates::Mask srcState, TStringView operationName);
    [[nodiscard]] b8 validateStagingTextureCopyResources(StagingTexture& stagingTexture, Texture& texture, CpuAccessMode::Enum requiredCpuAccess, VkImageUsageFlags requiredImageUsage, TStringView operationName);
    Expected<VkBufferImageCopy> prepareStagingTextureCopy(StagingTexture& stagingResource, const TextureSlice& stagingSlice, Texture& textureResource, const TextureSlice& textureSlice)const noexcept;
    [[nodiscard]] Expected<BufferSuballocation> prepareUploadStaging(usize dataSize, TStringView operationName, u32 alignment = s_DefaultUploadSuballocationAlignment);
    [[nodiscard]] Expected<BufferSuballocation> prepareUploadStaging(const void* data, usize dataSize, TStringView operationName, u32 alignment = s_DefaultUploadSuballocationAlignment);
    [[nodiscard]] Expected<VkDeviceAddress> suballocateBuildScratchAddress(u64 buildScratchSize, u64 scratchAlignment, TStringView operationName);
    // Returns mapped CPU-written build-input bytes whose device address is aligned. The pool reuses them only after the owning queue completes.
    [[nodiscard]] Expected<BufferSuballocation> suballocateBuildInput(u64 byteCount, u64 deviceAddressAlignment, TStringView operationName);
    [[nodiscard]] Expected<b8> validateAccelStructBuildSignature(
        AccelStruct& accelStruct,
        VkAccelerationStructureTypeKHR accelStructType,
        VkBuildAccelerationStructureFlagsKHR buildFlags,
        const AccelStructGeometryBuildSignature* geometrySignatures,
        usize geometrySignatureCount,
        b8 performUpdate,
        TStringView operationName
    );
    b8 buildTopLevelAccelStructFromInstanceData(
        RayTracingAccelStruct& as,
        VkDeviceAddress instanceDataAddress,
        usize numInstances,
        RayTracingAccelStructBuildFlags::Mask buildFlags,
        VkBuildAccelerationStructureFlagsKHR vkBuildFlags,
        TStringView operationName,
        const RayTracingInstanceDesc* instances = nullptr
    );
    [[nodiscard]] CommandMarkerRecordingToken beginMarkerLease(const AStringView name);
    [[nodiscard]] b8 endMarkerLease(const CommandMarkerRecordingToken& token);
    void abandonMarkerLease(const CommandMarkerRecordingToken& token)noexcept;
    [[nodiscard]] b8 markerLeaseMatchesTop(const CommandMarkerRecordingToken& token)const noexcept;
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
    b8 m_enableAutomaticBarriers = true;
    b8 m_isRecording = false;
    b8 m_commandRecordingFailed = false;
    b8 m_renderPassActive = false;
    b8 m_descriptorBuffersBound = false;
    u64 m_nativeRecordingID = 0u;
    u64 m_nextMarkerSerial = 0u;
    GraphicsVector<MarkerStackEntry> m_markerStack;
    Framebuffer* m_renderPassFramebuffer = nullptr;
#if defined(NWB_DEBUG)
    GpuQueueCapability::Mask m_taskCapabilitiesUsed = GpuQueueCapability::None;
    GpuQueueCapability::Mask m_taskDeclaredCapabilities = GpuQueueCapability::None;
    b8 m_taskCapabilityTracking = false;
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
    HashMap<TextureSubresourceStateKey, GpuPhysicalQueueId, Alloc::GlobalArena, TextureSubresourceStateKeyHasher, TextureSubresourceStateKeyEqualTo> m_textureOwnershipReleaseDestinations;
    HashMap<NotNull<Buffer*>, Vector<BufferOwnershipRelease, Alloc::GlobalArena>, Alloc::GlobalArena, Hasher<NotNull<Buffer*>>, EqualTo<NotNull<Buffer*>>> m_bufferOwnershipReleaseDestinations;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class EventQuery final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;


public:
    EventQuery(const VulkanContext& context);
    ~EventQuery()noexcept;


private:
    VkFence m_fence = VK_NULL_HANDLE;
    const VulkanContext& m_context;
    Futex m_mutex;

    b8 m_started = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    ~TimerQuery()noexcept;


public:
    // The caller owns this unaccepted recording transaction; its command buffers cannot submit concurrently.
    [[nodiscard]] b8 discardUnacceptedRecording(const TimerQueryRecordingToken& token)noexcept;
    // The caller has made its prior endpoint packet permanently non-submittable.
    // Preserve the accepted begin cycle while relinquishing only the stale end owner so a recovery command buffer can record its replacement endpoint.
    [[nodiscard]] b8 releaseUnacceptedEndForRecovery(const TimerQueryRecordingToken& token)noexcept;


private:
    // Caller holds m_mutex while consulting prerequisite and completed-cycle predicates.
    [[nodiscard]] QueueSubmissionToken prerequisiteSubmissionWithinLock()const noexcept{
        return m_completedCycleSubmission.valid() ? m_completedCycleSubmission : m_resetAuthorizationSubmission;
    }
    [[nodiscard]] b8 hasCompletedCycleWithinLock()const noexcept;
    [[nodiscard]] b8 completedCycleMatchesWithinLock(u64 generation, const QueueSubmissionToken& submission, const GpuPhysicalQueueInfo* queueInfo)const noexcept;


private:
    Futex m_mutex;
    VkQueryPool m_queryPool = VK_NULL_HANDLE;
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
    b8 m_cycleBaselineActive = false;
    b8 m_beginAccepted = false;
    b8 m_cycleInvalidated = false;
    b8 m_resetAuthorizationAvailable = false;
    b8 m_recordingActive = false;
    GpuPhysicalQueueId m_timestampQueue;
    GpuPhysicalQueueId m_cycleBaselineQueue;
    GpuPhysicalQueueId m_cycleQueue;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

