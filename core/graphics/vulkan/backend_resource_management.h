// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"
#include "backend_forward.h"
#include "backend_core_context.h"

#include <core/graphics/rhi/resource_state_handoff_key.h>
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


class BindingLayout final : public RefCounter<GraphicsResource>{
    friend class Device;
    friend class CommandList;
    friend class GpuDescriptorHeap;


public:
    BindingLayout(const VulkanContext& context);
    ~BindingLayout()noexcept;


public:
    [[nodiscard]] const BindingLayoutDesc* getDescription()const noexcept{ return m_isBindless ? nullptr : &m_desc; }

    [[nodiscard]] const BindlessLayoutDesc* getBindlessDesc()const noexcept{ return m_isBindless ? &m_bindlessDesc : nullptr; }


public:
    [[nodiscard]] b8 isDescriptorBufferCompatible()const noexcept{ return m_descriptorBufferCompatible; }

    [[nodiscard]] u32 getDescriptorBufferSetSizeBytes()const noexcept{ return m_descriptorBufferSetSizeBytes; }

    [[nodiscard]] DescriptorBufferSegmentKind::Enum getDescriptorBufferSegmentKind()const noexcept{ return m_descriptorBufferSegmentKind; }

    [[nodiscard]] const HashMap<u32, u32, Alloc::GlobalArena, Hasher<u32>, EqualTo<u32>>& getDescriptorBufferBindingOffsets()const noexcept{ return m_descriptorBufferBindingOffsets; }


private:
    BindingLayoutDesc m_desc;
    BindlessLayoutDesc m_bindlessDesc;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    Vector<VkDescriptorSetLayout, Alloc::GlobalArena> m_descriptorSetLayouts;
    u32 m_descriptorBufferSetSizeBytes = 0;
    u32 m_pushConstantByteSize = 0;
    DescriptorBufferSegmentKind::Enum m_descriptorBufferSegmentKind = DescriptorBufferSegmentKind::None;
    b8 m_isBindless = false;
    b8 m_descriptorBufferCompatible = false;
    HashMap<u32, u32, Alloc::GlobalArena, Hasher<u32>, EqualTo<u32>> m_descriptorBufferBindingOffsets;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AccelStruct final : public RefCounter<GraphicsResource>{
    friend class Device;
    friend class CommandList;
    friend class DescriptorBufferManager;
    friend class GpuDescriptorHeap;
    friend class TrackedCommandBuffer;
    friend class AccelStructBuildJournal;


public:
    AccelStruct(const VulkanContext& context, ResourceQueueSharing::Mask creationQueueSharing = ResourceQueueSharing::Exclusive)noexcept;
    ~AccelStruct()noexcept;


public:
    [[nodiscard]] const RayTracingAccelStructDesc& getDescription()const noexcept{ return m_desc; }

    [[nodiscard]] ResourceQueueSharing::Mask getCreationQueueSharing()const noexcept{ return m_creationQueueSharing; }

    [[nodiscard]] b8 queueSharingMatchesCreation()const noexcept{ return m_desc.queueSharing == m_creationQueueSharing; }

    [[nodiscard]] u16 getDeviceGeneration()const noexcept{ return m_context.deviceGeneration; }

    [[nodiscard]] u64 getDeviceAddress()const noexcept{ return m_deviceAddress; }
    [[nodiscard]] Buffer* getBackingBuffer()const noexcept{ return m_buffer.get(); }
    [[nodiscard]] const BufferHandle& getBackingBufferHandle()const noexcept{ return m_buffer; }
    Object getNativeHandle(ObjectType objectType)noexcept;


private:
    [[nodiscard]] GlobalUniquePtr<AccelStructBuildSignatureRole> acquireBuildSignatureRole();
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
    b8 m_isTopLevelAtCreation = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class OpacityMicromap final : public RefCounter<GraphicsResource>{
    friend class Device;
    friend class CommandList;
    friend class TrackedCommandBuffer;


public:
    OpacityMicromap(const VulkanContext& context)noexcept;
    ~OpacityMicromap()noexcept;


public:
    [[nodiscard]] const RayTracingOpacityMicromapDesc& getDescription()const noexcept{ return m_desc; }

    [[nodiscard]] u64 getDeviceAddress()const noexcept{ return m_deviceAddress; }
    [[nodiscard]] Buffer* getDataBuffer()const noexcept{ return m_dataBuffer.get(); }
    [[nodiscard]] VkMicromapEXT getMicromap()const noexcept{ return m_micromap; }
    [[nodiscard]] u32 getMaxOpacity2StateSubdivisionLevel()const noexcept{ return m_maxOpacity2StateSubdivisionLevel; }
    [[nodiscard]] u32 getMaxOpacity4StateSubdivisionLevel()const noexcept{ return m_maxOpacity4StateSubdivisionLevel; }


private:
    RayTracingOpacityMicromapDesc m_desc;
    BufferHandle m_dataBuffer;
    VkMicromapEXT m_micromap = VK_NULL_HANDLE;
    u64 m_deviceAddress = 0;

    const VulkanContext& m_context;
    Atomic<b8> m_acceptedConstructed{ false };
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
    class BufferRangeStates final{
    public:
        static constexpr usize s_InlineCapacity = 2u;


    public:
        BufferRangeStates()noexcept = default;
        explicit BufferRangeStates(Alloc::GlobalArena& arena)noexcept
            : m_arena(&arena)
        {}
        BufferRangeStates(const BufferRangeStates& other)
            : m_arena(other.m_arena)
        {
            assign(other.data(), other.size());
        }
        BufferRangeStates& operator=(const BufferRangeStates& other){
            if(this != &other){
                m_arena = other.m_arena;
                assign(other.data(), other.size());
            }
            return *this;
        }
        BufferRangeStates(BufferRangeStates&& other)noexcept
            : m_inline(other.m_inline)
            , m_overflow(Move(other.m_overflow))
            , m_arena(other.m_arena)
            , m_size(other.m_size)
        {
            other.m_size = 0u;
            other.m_overflow.reset();
        }
        BufferRangeStates& operator=(BufferRangeStates&& other)noexcept{
            if(this != &other){
                m_inline = other.m_inline;
                m_overflow = Move(other.m_overflow);
                m_arena = other.m_arena;
                m_size = other.m_size;
                other.m_size = 0u;
                other.m_overflow.reset();
            }
            return *this;
        }


    public:
        [[nodiscard]] b8 empty()const noexcept{ return size() == 0u; }
        [[nodiscard]] usize size()const noexcept{ return m_overflow ? m_overflow->size() : m_size; }
        [[nodiscard]] BufferRangeState* data()noexcept{ return m_overflow ? m_overflow->data() : m_inline.data(); }
        [[nodiscard]] const BufferRangeState* data()const noexcept{ return m_overflow ? m_overflow->data() : m_inline.data(); }
        [[nodiscard]] BufferRangeState* begin()noexcept{ return data(); }
        [[nodiscard]] const BufferRangeState* begin()const noexcept{ return data(); }
        [[nodiscard]] BufferRangeState* end()noexcept{ return data() + size(); }
        [[nodiscard]] const BufferRangeState* end()const noexcept{ return data() + size(); }
        [[nodiscard]] BufferRangeState& operator[](const usize index)noexcept{ return data()[index]; }
        [[nodiscard]] const BufferRangeState& operator[](const usize index)const noexcept{ return data()[index]; }
        [[nodiscard]] BufferRangeState& front()noexcept{ return data()[0u]; }
        [[nodiscard]] const BufferRangeState& front()const noexcept{ return data()[0u]; }
        [[nodiscard]] BufferRangeState& back()noexcept{ return data()[size() - 1u]; }
        [[nodiscard]] const BufferRangeState& back()const noexcept{ return data()[size() - 1u]; }

        void clear()noexcept{
            if(m_overflow)
                m_overflow->clear();
            m_size = 0u;
        }

        void reserve(const usize capacity){
            if(capacity <= s_InlineCapacity && !m_overflow)
                return;
            ensureOverflow();
            m_overflow->reserve(capacity);
        }

        void resize(const usize newSize){
            if(m_overflow){
                m_overflow->resize(newSize);
                m_size = newSize;
                return;
            }
            if(newSize <= s_InlineCapacity){
                for(usize i = m_size; i < newSize; ++i)
                    m_inline[i] = BufferRangeState{};
                m_size = newSize;
                return;
            }
            ensureOverflow();
            m_overflow->resize(newSize);
            m_size = newSize;
        }

        void push_back(const BufferRangeState& value){
            if(m_overflow){
                m_overflow->push_back(value);
                m_size = m_overflow->size();
                return;
            }
            if(m_size < s_InlineCapacity){
                m_inline[m_size++] = value;
                return;
            }
            ensureOverflow();
            m_overflow->push_back(value);
            m_size = m_overflow->size();
        }

        void assign(const BufferRangeState* const src, const usize count){
            if(!m_overflow && count <= s_InlineCapacity){
                for(usize i = 0u; i < count; ++i)
                    m_inline[i] = src[i];
                m_size = count;
                return;
            }
            ensureOverflow();
            if(count == 0u)
                m_overflow->clear();
            else
                m_overflow->assign(src, src + count);
            m_size = count;
        }


    private:
        void ensureOverflow(){
            if(m_overflow)
                return;
            FOUNDATION_ASSERT(m_arena);
            m_overflow.emplace(*m_arena);
            if(m_size != 0u){
                m_overflow->reserve(Max<usize>(m_size, s_InlineCapacity * 2u));
                for(usize i = 0u; i < m_size; ++i)
                    m_overflow->push_back(m_inline[i]);
            }
        }


    private:
        Array<BufferRangeState, s_InlineCapacity> m_inline{};
        Optional<Vector<BufferRangeState, Alloc::GlobalArena>> m_overflow;
        Alloc::GlobalArena* m_arena = nullptr;
        usize m_size = 0u;
    };


private:
    struct BufferUavBarrierPolicyValue{
        BufferHandle buffer;
        b8 enableBarriers = true;
    };

    struct TextureUavBarrierPolicyValue{
        TextureHandle texture;
        b8 enableBarriers = true;
    };


private:
    using PermanentTextureStateMap = HashMap<NotNull<Texture*>, PermanentTextureStateValue, Alloc::GlobalArena, Hasher<NotNull<Texture*>>, EqualTo<NotNull<Texture*>>>;
    using PermanentBufferStateMap = HashMap<NotNull<Buffer*>, PermanentBufferStateValue, Alloc::GlobalArena, Hasher<NotNull<Buffer*>>, EqualTo<NotNull<Buffer*>>>;
    using BufferUavBarrierPolicyMap = HashMap<NotNull<Buffer*>, BufferUavBarrierPolicyValue, Alloc::GlobalArena, Hasher<NotNull<Buffer*>>, EqualTo<NotNull<Buffer*>>>;
    using TextureUavBarrierPolicyMap = HashMap<NotNull<Texture*>, TextureUavBarrierPolicyValue, Alloc::GlobalArena, Hasher<NotNull<Texture*>>, EqualTo<NotNull<Texture*>>>;


public:
    StateTracker(const VulkanContext& context)
        : m_permanentTextureStates(0, Hasher<NotNull<Texture*>>(), EqualTo<NotNull<Texture*>>(), context.objectArena)
        , m_permanentBufferStates(0, Hasher<NotNull<Buffer*>>(), EqualTo<NotNull<Buffer*>>(), context.objectArena)
        , m_attemptPermanentTextures(context.objectArena)
        , m_attemptPermanentBuffers(context.objectArena)
        , m_textureStates(0, TextureSubresourceStateKeyHasher(), TextureSubresourceStateKeyEqualTo(), context.objectArena)
        , m_bufferStates(0, Hasher<NotNull<Buffer*>>(), EqualTo<NotNull<Buffer*>>(), context.objectArena)
        , m_textureUavBarriers(0, Hasher<NotNull<Texture*>>(), EqualTo<NotNull<Texture*>>(), context.objectArena)
        , m_bufferUavBarriers(0, Hasher<NotNull<Buffer*>>(), EqualTo<NotNull<Buffer*>>(), context.objectArena)
        , m_context(context)
    {}
    ~StateTracker()noexcept{}


public:
    void reset()noexcept{
        invalidateBufferCache();
        invalidateTextureCache();
        if(!m_textureStates.empty())
            m_textureStates.clear();
        if(!m_bufferStates.empty())
            m_bufferStates.clear();
        m_hasReadbackBuffers = false;
        m_hasKeepInitialStateTextures = false;
        m_hasKeepInitialStateBuffers = false;
    }
    void beginRecordingAttempt()noexcept;
    void commitRecordingAttempt()noexcept;
    void commitRecordingAttemptFast()noexcept{
        if(m_recordingAttemptActive && m_attemptPermanentTextures.empty() && m_attemptPermanentBuffers.empty()){
            m_recordingAttemptActive = false;
            return;
        }
        commitRecordingAttempt();
    }
    void rollbackRecordingAttempt()noexcept;
    void setPermanentTextureState(Texture& texture, ResourceStates::Mask state);
    void setPermanentBufferState(Buffer& buffer, ResourceStates::Mask state);

    [[nodiscard]] b8 hasPermanentTextureStates()const noexcept{ return !m_permanentTextureStates.empty(); }
    [[nodiscard]] b8 hasPermanentBufferStates()const noexcept{ return !m_permanentBufferStates.empty(); }

    [[nodiscard]] b8 isPermanentTexture(Texture& texture)const noexcept{
        return !m_permanentTextureStates.empty() && m_permanentTextureStates.find(MakeNotNull(&texture)) != m_permanentTextureStates.end();
    }

    [[nodiscard]] b8 isPermanentBuffer(Buffer& buffer)const noexcept{
        return !m_permanentBufferStates.empty() && m_permanentBufferStates.find(MakeNotNull(&buffer)) != m_permanentBufferStates.end();
    }
    [[nodiscard]] ResourceStates::Mask getPermanentTextureState(Texture* texture)const noexcept;
    [[nodiscard]] ResourceStates::Mask getPermanentBufferState(Buffer* buffer)const noexcept;
    [[nodiscard]] ResourceStates::Mask getTextureState(Texture* texture, ArraySlice arraySlice, MipLevel mipLevel)const noexcept;
    [[nodiscard]] ResourceStates::Mask getBufferState(Buffer* buffer, BufferRange range = s_EntireBuffer)const noexcept;
    [[nodiscard]] b8 hasExplicitTextureSubresourceState(Texture* texture, ArraySlice arraySlice, MipLevel mipLevel)const noexcept;
    [[nodiscard]] b8 hasExplicitBufferState(Buffer* buffer, BufferRange range = s_EntireBuffer, b8 requireKnown = false)const noexcept;

    void beginTrackingTexture(Texture& texture, TextureSubresourceSet subresources, ResourceStates::Mask state);
    void beginTrackingBuffer(Buffer* buffer, ResourceStates::Mask state, BufferRange range = s_EntireBuffer);
    void appendKeepInitialStateBarriers(TrackedCommandBuffer& commandBuffer, Vector<VkImageMemoryBarrier2, Alloc::GlobalArena>& imageBarriers, Vector<VkBufferMemoryBarrier2, Alloc::GlobalArena>& bufferBarriers);

    [[nodiscard]] b8 isUavBarrierEnabledForTexture(Texture& texture)const noexcept;
    [[nodiscard]] b8 isUavBarrierEnabledForBuffer(Buffer& buffer)const noexcept;
    void setEnableUavBarriersForTexture(Texture& texture, b8 enableBarriers);
    void setEnableUavBarriersForBuffer(Buffer& buffer, b8 enableBarriers);


private:
    [[nodiscard]] Expected<ResourceStates::Mask> getTransientTextureState(Texture& texture, ArraySlice arraySlice, MipLevel mipLevel)const noexcept;
    [[nodiscard]] ResourceStates::Mask getResolvedTransientTextureState(Texture& texture, ArraySlice arraySlice, MipLevel mipLevel)const noexcept;

    void beginTrackingTransientTexture(Texture& texture, TextureSubresourceSet subresources, ResourceStates::Mask state);
    void beginTrackingResolvedTransientTexture(Texture& texture, const TextureSubresourceSet& resolvedSubresources, ResourceStates::Mask state);
    void seedResolvedTransientTexture(Texture& texture, const TextureSubresourceSet& resolvedSubresources, ResourceStates::Mask state);
    struct RecentBufferCacheEntry{
        Buffer* key = nullptr;
        BufferRangeStates* ranges = nullptr;
    };

    struct RecentTextureCacheEntry{
        TextureSubresourceStateKey key{};
        ResourceStates::Mask state = ResourceStates::Unknown;
        b8 valid = false;
    };

    void invalidateBufferCache()const noexcept{
        m_recentBufferCache.fill(RecentBufferCacheEntry{});
        m_recentBufferCursor = 0u;
    }
    void recordRecentBufferRanges(Buffer& buffer, BufferRangeStates& ranges)const noexcept{
        for(RecentBufferCacheEntry& entry : m_recentBufferCache){
            if(entry.key == &buffer){
                entry.ranges = &ranges;
                return;
            }
        }
        m_recentBufferCache[m_recentBufferCursor] = RecentBufferCacheEntry{ &buffer, &ranges };
        m_recentBufferCursor = static_cast<u8>((m_recentBufferCursor + 1u) & 3u);
    }
    void invalidateTextureCache()const noexcept{
        m_recentTextureCache.fill(RecentTextureCacheEntry{});
        m_recentTextureCursor = 0u;
    }
    [[nodiscard]] b8 findRecentTextureState(const TextureSubresourceStateKey& key, ResourceStates::Mask& outState)const noexcept{
        for(const RecentTextureCacheEntry& entry : m_recentTextureCache){
            if(entry.valid && TextureSubresourceStateKeyEqualTo{}(entry.key, key)){
                outState = entry.state;
                return true;
            }
        }
        return false;
    }
    void updateRecentTextureCache(const TextureSubresourceStateKey& key, const ResourceStates::Mask state)const noexcept{
        for(RecentTextureCacheEntry& entry : m_recentTextureCache){
            if(entry.valid && TextureSubresourceStateKeyEqualTo{}(entry.key, key)){
                entry.state = state;
                return;
            }
        }
        m_recentTextureCache[m_recentTextureCursor] = RecentTextureCacheEntry{ key, state, true };
        m_recentTextureCursor = static_cast<u8>((m_recentTextureCursor + 1u) & 3u);
    }
    [[nodiscard]] const BufferRangeStates* findBufferRangesCached(Buffer& buffer)const noexcept;
    [[nodiscard]] BufferRangeStates* findBufferRangesCachedMutable(Buffer& buffer)noexcept;
    [[nodiscard]] ResourceStates::Mask getBufferStateFromRanges(Buffer& buffer, const BufferRangeStates* ranges, const BufferRange& resolvedRange)const noexcept;
    [[nodiscard]] b8 hasExplicitBufferStateFromRanges(const BufferRangeStates* ranges, const BufferRange& resolvedRange, b8 requireKnown = false)const noexcept;
    [[nodiscard]] static const BufferRangeState* FindFirstPotentiallyOverlappingRange(const BufferRangeStates& ranges, u64 byteOffset)noexcept;
    void beginTrackingTransientBuffer(Buffer& buffer, ResourceStates::Mask state, BufferRange range = s_EntireBuffer, b8 seedOnly = false);
    void beginTrackingTransientBufferWithRanges(Buffer& buffer, ResourceStates::Mask state, const BufferRange& resolvedRange, b8 seedOnly, BufferRangeStates* existingRanges);


private:
    PermanentTextureStateMap m_permanentTextureStates;
    PermanentBufferStateMap m_permanentBufferStates;
    Vector<NotNull<Texture*>, Alloc::GlobalArena> m_attemptPermanentTextures;
    Vector<NotNull<Buffer*>, Alloc::GlobalArena> m_attemptPermanentBuffers;
    HashMap<TextureSubresourceStateKey, ResourceStates::Mask, Alloc::GlobalArena, TextureSubresourceStateKeyHasher, TextureSubresourceStateKeyEqualTo> m_textureStates;
    HashMap<NotNull<Buffer*>, BufferRangeStates, Alloc::GlobalArena, Hasher<NotNull<Buffer*>>, EqualTo<NotNull<Buffer*>>> m_bufferStates;
    TextureUavBarrierPolicyMap m_textureUavBarriers;
    BufferUavBarrierPolicyMap m_bufferUavBarriers;
    mutable Array<RecentBufferCacheEntry, 4u> m_recentBufferCache{};
    mutable Array<RecentTextureCacheEntry, 4u> m_recentTextureCache{};
    mutable u8 m_recentBufferCursor = 0u;
    mutable u8 m_recentTextureCursor = 0u;

    const VulkanContext& m_context;
    b8 m_recordingAttemptActive = false;
    b8 m_hasReadbackBuffers = false;
    b8 m_hasKeepInitialStateTextures = false;
    b8 m_hasKeepInitialStateBuffers = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

