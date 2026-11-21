// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Framebuffer;
struct VulkanContext;


struct RetainedBufferStateCommit{
    NotNull<Buffer*> buffer;
};

class CommandBufferResourceReferences final : NoCopy{
    friend class CommandList;
    friend class TrackedCommandBuffer;


private:
    static constexpr usize s_CacheSlotCount = 64u;
    static constexpr usize s_CacheSlotMask = s_CacheSlotCount - 1u;
    static constexpr u8 s_FlagOwned = 1u << 0u;
    static constexpr u8 s_FlagBuffer = 1u << 1u;
    static constexpr u8 s_FlagTexture = 1u << 2u;
    static constexpr u8 s_FlagBufferStateCommit = 1u << 3u;
    static constexpr u8 s_FlagFramebuffer = 1u << 4u;

    struct CacheSlot{
        GraphicsResource* resource = nullptr;
        u32 generation = 0u;
        u8 flags = 0u;
    };

    struct MembershipEntry{
        GraphicsResource* resource = nullptr;
        u32 generation = 0u;
        u8 flags = 0u;
    };

    [[nodiscard]] static FOUNDATION_INLINE usize CacheSlotIndex(const GraphicsResource* const ptr)noexcept{
        const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        return static_cast<usize>(((addr >> 6u) ^ (addr >> 12u)) & s_CacheSlotMask);
    }

    [[nodiscard]] static FOUNDATION_INLINE u64 HashResourcePointer(const GraphicsResource* const ptr)noexcept{
        u64 x = static_cast<u64>(reinterpret_cast<uintptr_t>(ptr));
        x ^= x >> 33u;
        x *= 0xff51afd7ed558ccduLL;
        x ^= x >> 33u;
        x *= 0xc4ceb9fe1a85ec53uLL;
        x ^= x >> 33u;
        return x;
    }


public:
    explicit CommandBufferResourceReferences(const VulkanContext& context)noexcept;
    ~CommandBufferResourceReferences()noexcept = default;


public:
    FOUNDATION_INLINE void retainResource(GraphicsResource& resource){
        GraphicsResource* const key = &resource;
        if(m_lastRetainedResource == key)
            return;
        const CacheSlot& slot = m_cache[CacheSlotIndex(key)];
        if(slot.resource == key && slot.generation == m_cacheGeneration && (slot.flags & s_FlagOwned) != 0u){
            m_lastRetainedResource = key;
            return;
        }
        retainResourceSlow(resource);
    }
    inline void retainBuffer(Buffer& buffer);
    inline void trackRetainedBuffer(Buffer& buffer);
    void appendBufferStateCommit(Buffer& buffer);
    void discardBufferStateCommits()noexcept;
    inline void retainTexture(Texture& texture);
    inline void trackRetainedTexture(Texture& texture);
    inline void retainFramebuffer(Framebuffer& framebuffer);
    void clear()noexcept;


private:
    void retainResourceSlow(GraphicsResource& resource);
    void retainBufferSlow(Buffer& buffer);
    void trackRetainedBufferSlow(Buffer& buffer);
    void retainTextureSlow(Texture& texture);
    void trackRetainedTextureSlow(Texture& texture);
    void retainFramebufferSlow(Framebuffer& framebuffer);
    FOUNDATION_INLINE void updateCacheFlags(GraphicsResource* const key, const u8 addFlags)noexcept{
        CacheSlot& slot = m_cache[CacheSlotIndex(key)];
        if(slot.resource == key && slot.generation == m_cacheGeneration)
            slot.flags |= addFlags;
        else{
            slot.resource = key;
            slot.generation = m_cacheGeneration;
            slot.flags = addFlags;
        }
    }
    void promoteMembershipIndex();
    void rehashMembershipTable(usize newCapacity);
    void pushRetainedBuffer(Buffer& buffer);
    void pushRetainedTexture(Texture& texture);
    [[nodiscard]] MembershipEntry& findOrInsertMembership(GraphicsResource* key);
    [[nodiscard]] MembershipEntry* findMembershipEntry(GraphicsResource* key)noexcept;


private:
    Vector<Handle<GraphicsResource>, Alloc::GlobalArena> m_resources;
    Vector<NotNull<Buffer*>, Alloc::GlobalArena> m_buffers;
    Vector<NotNull<Texture*>, Alloc::GlobalArena> m_textures;
    Vector<RetainedBufferStateCommit, Alloc::GlobalArena> m_bufferStateCommits;
    Array<CacheSlot, s_CacheSlotCount> m_cache{};
    Vector<MembershipEntry, Alloc::GlobalArena> m_membershipTable;
    GraphicsResource* m_lastRetainedResource = nullptr;
    Buffer* m_lastRetainedBuffer = nullptr;
    Texture* m_lastRetainedTexture = nullptr;
    Framebuffer* m_lastRetainedFramebuffer = nullptr;
    usize m_membershipCount = 0u;
    u32 m_cacheGeneration = 1u;
    b8 m_hasDynamicReadinessResources = false;
    b8 m_hasCloseValidationTextures = false;
    b8 m_hasCloseValidationBuffers = false;
    b8 m_usingMembershipTable = false;
    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

