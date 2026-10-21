// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"
#include "backend_forward.h"

#include <core/graphics/rhi/resource_state_handoff_key.h>
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


class BindingLayout final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    BindingLayout(const VulkanContext& context);
    ~BindingLayout()noexcept;


public:
    [[nodiscard]] const BindingLayoutDesc* getDescription()const noexcept{ return m_isBindless ? nullptr : &m_desc; }
    [[nodiscard]] const BindlessLayoutDesc* getBindlessDesc()const noexcept{ return m_isBindless ? &m_bindlessDesc : nullptr; }

public:
    // Descriptor-buffer metadata; layouts must be pure resource or sampler sets.
    [[nodiscard]] bool isDescriptorBufferCompatible()const noexcept{ return m_descriptorBufferCompatible; }
    [[nodiscard]] u32 getDescriptorBufferSetSizeBytes()const noexcept{ return m_descriptorBufferSetSizeBytes; }
    [[nodiscard]] DescriptorBufferSegmentKind::Enum getDescriptorBufferSegmentKind()const noexcept{ return m_descriptorBufferSegmentKind; }
    [[nodiscard]] const HashMap<u32, u32, Alloc::GlobalArena, Hasher<u32>, EqualTo<u32>>& getDescriptorBufferBindingOffsets()const noexcept{ return m_descriptorBufferBindingOffsets; }


private:
    BindingLayoutDesc m_desc;
    BindlessLayoutDesc m_bindlessDesc;
    // Only push-only layouts own a reusable zero-set pipeline layout.
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    Vector<VkDescriptorSetLayout, Alloc::GlobalArena> m_descriptorSetLayouts;
    // Driver-created set size, segment, and binding offsets.
    u32 m_descriptorBufferSetSizeBytes = 0;
    u32 m_pushConstantByteSize = 0;
    DescriptorBufferSegmentKind::Enum m_descriptorBufferSegmentKind = DescriptorBufferSegmentKind::None;
    bool m_isBindless = false;
    bool m_descriptorBufferCompatible = false;
    HashMap<u32, u32, Alloc::GlobalArena, Hasher<u32>, EqualTo<u32>> m_descriptorBufferBindingOffsets;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AccelStruct final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;
    friend class CommandList;
    friend class DescriptorBufferManager;
    friend class GpuDescriptorHeap;
    friend class TrackedCommandBuffer;


public:
    AccelStruct(const VulkanContext& context, ResourceQueueSharing::Mask creationQueueSharing = ResourceQueueSharing::Exclusive)noexcept;
    ~AccelStruct()noexcept;


public:
    [[nodiscard]] const RayTracingAccelStructDesc& getDescription()const noexcept{ return m_desc; }
    [[nodiscard]] ResourceQueueSharing::Mask getCreationQueueSharing()const noexcept{ return m_creationQueueSharing; }
    [[nodiscard]] bool queueSharingMatchesCreation()const noexcept{ return m_desc.queueSharing == m_creationQueueSharing; }
    [[nodiscard]] u16 getDeviceGeneration()const noexcept{ return m_context.deviceGeneration; }
    [[nodiscard]] u64 getDeviceAddress()const noexcept{ return m_deviceAddress; }
    // Exposed for explicit scheduling handoffs.
    [[nodiscard]] Buffer* getBackingBuffer()const noexcept{ return m_buffer.get(); }
    // Graph declarations retain this handle when a task must seed or transition the backing storage explicitly.
    [[nodiscard]] const BufferHandle& getBackingBufferHandle()const noexcept{ return m_buffer; }
    Object getNativeHandle(ObjectType objectType)noexcept;


private:
    void collectRetiredBuildSignatureRoles()noexcept;
    void retireBuildSignatureRole(AccelStructBuildSignatureRole& role)noexcept;


private:
    RayTracingAccelStructDesc m_desc;
    VkAccelerationStructureKHR m_accelStruct = VK_NULL_HANDLE;
    BufferHandle m_buffer;
    u64 m_deviceAddress = 0;
    mutable Futex m_memoryBindingMutex;
    mutable Futex m_acceptedBuildSignatureMutex;
    AccelStructBuildSignatureRole* m_acceptedBuildSignatureRole = nullptr;
    AccelStructBuildSignatureRole* m_retiredBuildSignatureRoles = nullptr;

    const VulkanContext& m_context;
    const ResourceQueueSharing::Mask m_creationQueueSharing;
    bool m_isTopLevelAtCreation = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class OpacityMicromap final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;
    friend class CommandList;
    friend class TrackedCommandBuffer;


public:
    OpacityMicromap(const VulkanContext& context)noexcept;
    ~OpacityMicromap()noexcept;


public:
    [[nodiscard]] const RayTracingOpacityMicromapDesc& getDescription()const noexcept{ return m_desc; }
    [[nodiscard]] u64 getDeviceAddress()const noexcept{ return m_deviceAddress; }


private:
    RayTracingOpacityMicromapDesc m_desc;
    BufferHandle m_dataBuffer;
    VkMicromapEXT m_micromap = VK_NULL_HANDLE;
    u64 m_deviceAddress = 0;

    const VulkanContext& m_context;
    Atomic<bool> m_acceptedConstructed{ false };
    u32 m_maxOpacity2StateSubdivisionLevel = 0u;
    u32 m_maxOpacity4StateSubdivisionLevel = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class StateTracker final : NoCopy{
    friend class CommandList;


private:
    struct PermanentTextureStateValue{
        TextureHandle texture;
        ResourceStates::Mask state = ResourceStates::Unknown;
    };

    struct PermanentBufferStateValue{
        BufferHandle buffer;
        ResourceStates::Mask state = ResourceStates::Unknown;
    };

    struct BufferRangeState{
        BufferRange range;
        ResourceStates::Mask state = ResourceStates::Unknown;
    };


private:
    using BufferRangeStates = Vector<BufferRangeState, Alloc::GlobalArena>;


private:
    struct BufferUavBarrierPolicyValue{
        BufferHandle buffer;
        bool enableBarriers = true;
    };

    struct TextureUavBarrierPolicyValue{
        TextureHandle texture;
        bool enableBarriers = true;
    };


private:
    using PermanentTextureStateMap = HashMap<NotNull<Texture*>, PermanentTextureStateValue, Alloc::GlobalArena, Hasher<NotNull<Texture*>>, EqualTo<NotNull<Texture*>>>;
    using PermanentBufferStateMap = HashMap<NotNull<Buffer*>, PermanentBufferStateValue, Alloc::GlobalArena, Hasher<NotNull<Buffer*>>, EqualTo<NotNull<Buffer*>>>;
    using BufferUavBarrierPolicyMap = HashMap<NotNull<Buffer*>, BufferUavBarrierPolicyValue, Alloc::GlobalArena, Hasher<NotNull<Buffer*>>, EqualTo<NotNull<Buffer*>>>;
    using TextureUavBarrierPolicyMap = HashMap<NotNull<Texture*>, TextureUavBarrierPolicyValue, Alloc::GlobalArena, Hasher<NotNull<Texture*>>, EqualTo<NotNull<Texture*>>>;


public:
    StateTracker(const VulkanContext& context);
    ~StateTracker()noexcept;


public:
    void reset()noexcept;
    void beginRecordingAttempt()noexcept;
    void commitRecordingAttempt()noexcept;
    void rollbackRecordingAttempt()noexcept;
    void setPermanentTextureState(Texture& texture, ResourceStates::Mask state);
    void setPermanentBufferState(Buffer& buffer, ResourceStates::Mask state);

    [[nodiscard]] bool isPermanentTexture(Texture& texture)const noexcept;
    [[nodiscard]] bool isPermanentBuffer(Buffer& buffer)const noexcept;
    [[nodiscard]] ResourceStates::Mask getPermanentTextureState(Texture* texture)const noexcept;
    [[nodiscard]] ResourceStates::Mask getPermanentBufferState(Buffer* buffer)const noexcept;
    [[nodiscard]] ResourceStates::Mask getTextureState(Texture* texture, ArraySlice arraySlice, MipLevel mipLevel)const noexcept;
    [[nodiscard]] ResourceStates::Mask getBufferState(Buffer* buffer, BufferRange range = s_EntireBuffer)const noexcept;
    // Explicit state comes from this command list or an imported packet handoff.
    // A keep-initial-state descriptor fallback deliberately does not count: graph lowering can still declare the first known graph state.
    [[nodiscard]] bool hasExplicitTextureSubresourceState(Texture* texture, ArraySlice arraySlice, MipLevel mipLevel)const noexcept;
    [[nodiscard]] bool hasExplicitBufferState(Buffer* buffer, BufferRange range = s_EntireBuffer, bool requireKnown = false)const noexcept;

    void beginTrackingTexture(Texture& texture, TextureSubresourceSet subresources, ResourceStates::Mask state);
    void beginTrackingBuffer(Buffer* buffer, ResourceStates::Mask state, BufferRange range = s_EntireBuffer);
    void appendKeepInitialStateBarriers(TrackedCommandBuffer& commandBuffer, Vector<VkImageMemoryBarrier2, Alloc::GlobalArena>& imageBarriers, Vector<VkBufferMemoryBarrier2, Alloc::GlobalArena>& bufferBarriers);

    [[nodiscard]] bool isUavBarrierEnabledForTexture(Texture& texture)const noexcept;
    [[nodiscard]] bool isUavBarrierEnabledForBuffer(Buffer& buffer)const noexcept;
    void setEnableUavBarriersForTexture(Texture& texture, bool enableBarriers);
    void setEnableUavBarriersForBuffer(Buffer& buffer, bool enableBarriers);


private:
    [[nodiscard]] Expected<ResourceStates::Mask> getTransientTextureState(Texture& texture, ArraySlice arraySlice, MipLevel mipLevel)const noexcept;
    [[nodiscard]] ResourceStates::Mask getResolvedTransientTextureState(Texture& texture, ArraySlice arraySlice, MipLevel mipLevel)const noexcept;

    void beginTrackingTransientTexture(Texture& texture, TextureSubresourceSet subresources, ResourceStates::Mask state);
    void beginTrackingResolvedTransientTexture(Texture& texture, const TextureSubresourceSet& resolvedSubresources, ResourceStates::Mask state);
    void beginTrackingTransientBuffer(Buffer& buffer, ResourceStates::Mask state, BufferRange range = s_EntireBuffer, bool seedOnly = false);


private:
    PermanentTextureStateMap m_permanentTextureStates;
    PermanentBufferStateMap m_permanentBufferStates;
    Vector<NotNull<Texture*>, Alloc::GlobalArena> m_attemptPermanentTextures;
    Vector<NotNull<Buffer*>, Alloc::GlobalArena> m_attemptPermanentBuffers;
    HashMap<TextureSubresourceStateKey, ResourceStates::Mask, Alloc::GlobalArena, TextureSubresourceStateKeyHasher, TextureSubresourceStateKeyEqualTo> m_textureStates;
    HashMap<NotNull<Buffer*>, BufferRangeStates, Alloc::GlobalArena, Hasher<NotNull<Buffer*>>, EqualTo<NotNull<Buffer*>>> m_bufferStates;
    TextureUavBarrierPolicyMap m_textureUavBarriers;
    BufferUavBarrierPolicyMap m_bufferUavBarriers;

    const VulkanContext& m_context;
    bool m_recordingAttemptActive = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

