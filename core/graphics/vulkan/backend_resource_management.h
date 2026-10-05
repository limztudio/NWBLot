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


// Binding Layout


class BindingLayout final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    BindingLayout(const VulkanContext& context);
    ~BindingLayout();


public:
    [[nodiscard]] const BindingLayoutDesc* getDescription()const{ return m_isBindless ? nullptr : &m_desc; }
    [[nodiscard]] const BindlessLayoutDesc* getBindlessDesc()const{ return m_isBindless ? &m_bindlessDesc : nullptr; }

public:
    // Descriptor-buffer metadata; layouts must be pure resource or sampler sets.
    [[nodiscard]] bool isDescriptorBufferCompatible()const{ return m_descriptorBufferCompatible; }
    [[nodiscard]] u32 getDescriptorBufferSetSizeBytes()const{ return m_descriptorBufferSetSizeBytes; }
    [[nodiscard]] DescriptorBufferSegmentKind::Enum getDescriptorBufferSegmentKind()const{ return m_descriptorBufferSegmentKind; }
    [[nodiscard]] const HashMap<u32, u32, Alloc::GlobalArena, Hasher<u32>, EqualTo<u32>>& getDescriptorBufferBindingOffsets()const{ return m_descriptorBufferBindingOffsets; }


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


// Acceleration Structure


class AccelStruct final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;
    friend class CommandList;
    friend class DescriptorBufferManager;
    friend class GpuDescriptorHeap;
    friend class TrackedCommandBuffer;

public:
    AccelStruct(
        const VulkanContext& context,
        ResourceQueueSharing::Mask creationQueueSharing = ResourceQueueSharing::Exclusive
    );
    ~AccelStruct()noexcept;


public:
    [[nodiscard]] const RayTracingAccelStructDesc& getDescription()const{ return m_desc; }
    [[nodiscard]] ResourceQueueSharing::Mask getCreationQueueSharing()const noexcept{ return m_creationQueueSharing; }
    [[nodiscard]] bool queueSharingMatchesCreation()const noexcept{ return m_desc.queueSharing == m_creationQueueSharing; }
    [[nodiscard]] u16 getDeviceGeneration()const noexcept{ return m_context.deviceGeneration; }
    [[nodiscard]] u64 getDeviceAddress()const{ return m_deviceAddress; }
    // Exposed for explicit scheduling handoffs.
    [[nodiscard]] Buffer* getBackingBuffer()const{ return m_buffer.get(); }
    // Graph declarations retain this handle when a task must seed or transition the backing storage explicitly.
    [[nodiscard]] const BufferHandle& getBackingBufferHandle()const{ return m_buffer; }
    Object getNativeHandle(ObjectType objectType);


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


// Opacity Micromap


class OpacityMicromap final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;
    friend class CommandList;
    friend class TrackedCommandBuffer;


public:
    OpacityMicromap(const VulkanContext& context);
    ~OpacityMicromap();


public:
    [[nodiscard]] const RayTracingOpacityMicromapDesc& getDescription()const{ return m_desc; }
    [[nodiscard]] u64 getDeviceAddress()const{ return m_deviceAddress; }


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


// State Tracker


struct TextureSubresourceStateKey{
    Texture* texture = nullptr;
    MipLevel mipLevel = 0;
    ArraySlice arraySlice = 0;
};

struct TextureSubresourceStateKeyHasher{
    [[nodiscard]] usize operator()(const TextureSubresourceStateKey& value)const noexcept{
        usize seed = 0;
        ::HashCombine(seed, value.texture);
        ::HashCombine(seed, value.mipLevel);
        ::HashCombine(seed, value.arraySlice);
        return seed;
    }
};

struct TextureSubresourceStateKeyEqualTo{
    [[nodiscard]] bool operator()(const TextureSubresourceStateKey& lhs, const TextureSubresourceStateKey& rhs)const noexcept{
        return lhs.texture == rhs.texture && lhs.mipLevel == rhs.mipLevel && lhs.arraySlice == rhs.arraySlice;
    }
};

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

    using BufferRangeStates = Vector<BufferRangeState, Alloc::GlobalArena>;

    struct BufferUavBarrierPolicyValue{
        BufferHandle buffer;
        bool enableBarriers = true;
    };

    struct TextureUavBarrierPolicyValue{
        TextureHandle texture;
        bool enableBarriers = true;
    };

    using PermanentTextureStateMap = HashMap<Texture*, PermanentTextureStateValue, Alloc::GlobalArena, Hasher<Texture*>, EqualTo<Texture*>>;
    using PermanentBufferStateMap = HashMap<Buffer*, PermanentBufferStateValue, Alloc::GlobalArena, Hasher<Buffer*>, EqualTo<Buffer*>>;
    using BufferUavBarrierPolicyMap = HashMap<Buffer*, BufferUavBarrierPolicyValue, Alloc::GlobalArena, Hasher<Buffer*>, EqualTo<Buffer*>>;
    using TextureUavBarrierPolicyMap = HashMap<Texture*, TextureUavBarrierPolicyValue, Alloc::GlobalArena, Hasher<Texture*>, EqualTo<Texture*>>;


public:
    StateTracker(const VulkanContext& context);
    ~StateTracker();


public:
    void reset();
    void beginRecordingAttempt();
    void commitRecordingAttempt()noexcept;
    void rollbackRecordingAttempt()noexcept;
    void setPermanentTextureState(Texture& texture, ResourceStates::Mask state);
    void setPermanentBufferState(Buffer& buffer, ResourceStates::Mask state);

    [[nodiscard]] bool isPermanentTexture(Texture& texture)const;
    [[nodiscard]] bool isPermanentBuffer(Buffer& buffer)const;
    [[nodiscard]] ResourceStates::Mask getPermanentTextureState(Texture* texture)const;
    [[nodiscard]] ResourceStates::Mask getPermanentBufferState(Buffer* buffer)const;
    [[nodiscard]] ResourceStates::Mask getTextureState(Texture* texture, ArraySlice arraySlice, MipLevel mipLevel)const;
    [[nodiscard]] ResourceStates::Mask getBufferState(Buffer* buffer, BufferRange range = s_EntireBuffer)const;
    // Explicit state comes from this command list or an imported packet handoff. A keep-initial-state descriptor
    // fallback deliberately does not count: graph lowering can still declare the first known graph state.
    [[nodiscard]] bool hasExplicitTextureSubresourceState(Texture* texture, ArraySlice arraySlice, MipLevel mipLevel)const;
    [[nodiscard]] bool hasExplicitBufferState(Buffer* buffer, BufferRange range = s_EntireBuffer, bool requireKnown = false)const;

    void beginTrackingTexture(Texture* texture, TextureSubresourceSet subresources, ResourceStates::Mask state);
    void beginTrackingBuffer(Buffer* buffer, ResourceStates::Mask state, BufferRange range = s_EntireBuffer);
    void appendKeepInitialStateBarriers(
        TrackedCommandBuffer& commandBuffer,
        Vector<VkImageMemoryBarrier2, Alloc::GlobalArena>& imageBarriers,
        Vector<VkBufferMemoryBarrier2, Alloc::GlobalArena>& bufferBarriers
    );

    [[nodiscard]] bool isUavBarrierEnabledForTexture(Texture& texture)const;
    [[nodiscard]] bool isUavBarrierEnabledForBuffer(Buffer& buffer)const;
    void setEnableUavBarriersForTexture(Texture& texture, bool enableBarriers);
    void setEnableUavBarriersForBuffer(Buffer& buffer, bool enableBarriers);


private:
    [[nodiscard]] bool getTransientTextureState(Texture& texture, ArraySlice arraySlice, MipLevel mipLevel, ResourceStates::Mask& outState)const;
    [[nodiscard]] bool getResolvedTransientTextureState(Texture& texture, ArraySlice arraySlice, MipLevel mipLevel, ResourceStates::Mask& outState)const;

    void beginTrackingTransientTexture(Texture& texture, TextureSubresourceSet subresources, ResourceStates::Mask state);
    void beginTrackingResolvedTransientTexture(Texture& texture, const TextureSubresourceSet& resolvedSubresources, ResourceStates::Mask state);
    void beginTrackingTransientBuffer(Buffer& buffer, ResourceStates::Mask state, BufferRange range = s_EntireBuffer, bool seedOnly = false);


private:
    PermanentTextureStateMap m_permanentTextureStates;
    PermanentBufferStateMap m_permanentBufferStates;
    Vector<Texture*, Alloc::GlobalArena> m_attemptPermanentTextures;
    Vector<Buffer*, Alloc::GlobalArena> m_attemptPermanentBuffers;
    HashMap<TextureSubresourceStateKey, ResourceStates::Mask, Alloc::GlobalArena, TextureSubresourceStateKeyHasher, TextureSubresourceStateKeyEqualTo> m_textureStates;
    HashMap<Buffer*, BufferRangeStates, Alloc::GlobalArena, Hasher<Buffer*>, EqualTo<Buffer*>> m_bufferStates;
    TextureUavBarrierPolicyMap m_textureUavBarriers;
    BufferUavBarrierPolicyMap m_bufferUavBarriers;

    const VulkanContext& m_context;
    bool m_recordingAttemptActive = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

