// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_buffer_resource_references.h"

#include "backend.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_command_buffer_resource_references{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_LinearCapacity = 32u;
inline constexpr usize s_InitialMembershipTableCapacity = 128u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CommandBufferResourceReferences::CommandBufferResourceReferences(const VulkanContext& context)noexcept
    : m_resources(context.objectArena)
    , m_buffers(context.objectArena)
    , m_textures(context.objectArena)
    , m_bufferStateCommits(context.objectArena)
    , m_membershipTable(context.objectArena)
    , m_context(context)
{}

void CommandBufferResourceReferences::pushRetainedBuffer(Buffer& buffer){
    if(!buffer.m_managed || buffer.m_creationDesc.isVirtual){
        m_hasDynamicReadinessResources = true;
        m_hasCloseValidationBuffers = true;
    }
    else if(
        buffer.m_bufferInfo.sharingMode == VK_SHARING_MODE_CONCURRENT
        || &buffer.m_context != &m_context
        || !buffer.m_gpuReady.load(MemoryOrder::acquire)
        || !buffer.getQueueAdmissionSnapshot().valid()
    )
        m_hasCloseValidationBuffers = true;
    m_buffers.push_back(MakeNotNull(&buffer));
}

void CommandBufferResourceReferences::pushRetainedTexture(Texture& texture){
    if(!texture.m_managed || texture.m_creationDesc.isVirtual){
        m_hasDynamicReadinessResources = true;
        m_hasCloseValidationTextures = true;
    }
    else if(
        texture.m_imageInfo.sharingMode == VK_SHARING_MODE_CONCURRENT
        || &texture.m_context != &m_context
        || !texture.m_gpuReady.load(MemoryOrder::acquire)
        || !texture.getQueueAdmissionSnapshot().valid()
    )
        m_hasCloseValidationTextures = true;
    m_textures.push_back(MakeNotNull(&texture));
}

void CommandBufferResourceReferences::rehashMembershipTable(const usize newCapacity){
    Vector<MembershipEntry, Alloc::GlobalArena> nextTable(m_context.objectArena);
    nextTable.resize(newCapacity);
    const usize mask = newCapacity - 1u;
    for(const MembershipEntry& entry : m_membershipTable){
        if(entry.generation != m_cacheGeneration)
            continue;
        usize index = static_cast<usize>(HashResourcePointer(entry.resource)) & mask;
        while(nextTable[index].generation == m_cacheGeneration)
            index = (index + 1u) & mask;
        nextTable[index] = entry;
    }
    m_membershipTable.swap(nextTable);
}

CommandBufferResourceReferences::MembershipEntry& CommandBufferResourceReferences::findOrInsertMembership(GraphicsResource* const key){
    using namespace __hidden_command_buffer_resource_references;
    if(m_membershipTable.empty())
        m_membershipTable.resize(s_InitialMembershipTableCapacity);
    else if((m_membershipCount + 1u) * 2u > m_membershipTable.size()){
        const usize nextCapacity = m_membershipTable.size() * 2u;
        rehashMembershipTable(nextCapacity);
    }

    const usize mask = m_membershipTable.size() - 1u;
    usize index = static_cast<usize>(HashResourcePointer(key)) & mask;
    for(;;){
        MembershipEntry& entry = m_membershipTable[index];
        if(entry.generation != m_cacheGeneration){
            entry.resource = key;
            entry.generation = m_cacheGeneration;
            entry.flags = 0u;
            ++m_membershipCount;
            return entry;
        }
        if(entry.resource == key)
            return entry;
        index = (index + 1u) & mask;
    }
}

CommandBufferResourceReferences::MembershipEntry* CommandBufferResourceReferences::findMembershipEntry(GraphicsResource* const key)noexcept{
    if(m_membershipTable.empty())
        return nullptr;

    const usize mask = m_membershipTable.size() - 1u;
    usize index = static_cast<usize>(HashResourcePointer(key)) & mask;
    for(;;){
        MembershipEntry& entry = m_membershipTable[index];
        if(entry.generation != m_cacheGeneration)
            return nullptr;
        if(entry.resource == key)
            return &entry;
        index = (index + 1u) & mask;
    }
}

void CommandBufferResourceReferences::retainResourceSlow(GraphicsResource& resource){
    using namespace __hidden_command_buffer_resource_references;
    GraphicsResource* const key = &resource;
    if(!m_usingMembershipTable){
        for(const Handle<GraphicsResource>& retainedResource : m_resources){
            if(retainedResource.get() == key){
                updateCacheFlags(key, s_FlagOwned);
                m_lastRetainedResource = key;
                return;
            }
        }
        if(m_resources.size() < s_LinearCapacity){
            m_resources.emplace_back(key, Handle<GraphicsResource>::deleter_type(&m_context.objectArena));
            updateCacheFlags(key, s_FlagOwned);
            m_lastRetainedResource = key;
            return;
        }
        promoteMembershipIndex();
    }

    MembershipEntry& membership = findOrInsertMembership(key);
    if((membership.flags & s_FlagOwned) == 0u){
        m_resources.emplace_back(key, Handle<GraphicsResource>::deleter_type(&m_context.objectArena));
        membership.flags |= s_FlagOwned;
    }
    updateCacheFlags(key, membership.flags);
    m_lastRetainedResource = key;
}

void CommandBufferResourceReferences::retainBufferSlow(Buffer& buffer){
    GraphicsResource* const key = &buffer;
    if(!m_usingMembershipTable){
        retainResource(buffer);
        trackRetainedBufferSlow(buffer);
        if(!m_usingMembershipTable){
            updateCacheFlags(key, s_FlagOwned | s_FlagBuffer);
            m_lastRetainedResource = key;
            m_lastRetainedBuffer = &buffer;
            return;
        }
    }

    MembershipEntry& membership = findOrInsertMembership(key);
    if((membership.flags & s_FlagOwned) == 0u){
        m_resources.emplace_back(key, Handle<GraphicsResource>::deleter_type(&m_context.objectArena));
        membership.flags |= s_FlagOwned;
    }
    if((membership.flags & s_FlagBuffer) == 0u){
        pushRetainedBuffer(buffer);
        membership.flags |= s_FlagBuffer;
    }
    updateCacheFlags(key, membership.flags);
    m_lastRetainedResource = key;
    m_lastRetainedBuffer = &buffer;
}

void CommandBufferResourceReferences::trackRetainedBufferSlow(Buffer& buffer){
    using namespace __hidden_command_buffer_resource_references;
    GraphicsResource* const key = &buffer;
    if(!m_usingMembershipTable){
        for(const auto retainedBuffer : m_buffers){
            if(retainedBuffer.get() == &buffer){
                updateCacheFlags(key, s_FlagBuffer);
                return;
            }
        }
        if(m_buffers.size() < s_LinearCapacity){
            pushRetainedBuffer(buffer);
            updateCacheFlags(key, s_FlagBuffer);
            return;
        }
        promoteMembershipIndex();
    }

    MembershipEntry& membership = findOrInsertMembership(key);
    if((membership.flags & s_FlagBuffer) == 0u){
        pushRetainedBuffer(buffer);
        membership.flags |= s_FlagBuffer;
    }
    updateCacheFlags(key, membership.flags);
}

void CommandBufferResourceReferences::appendBufferStateCommit(Buffer& buffer){
    GraphicsResource* const key = &buffer;
    const CacheSlot& slot = m_cache[CacheSlotIndex(key)];
    if(slot.resource == key && slot.generation == m_cacheGeneration && (slot.flags & (s_FlagOwned | s_FlagBuffer | s_FlagBufferStateCommit)) == (s_FlagOwned | s_FlagBuffer | s_FlagBufferStateCommit))
        return;

    if(!m_usingMembershipTable){
        retainResource(buffer);
        trackRetainedBuffer(buffer);
        if(!m_usingMembershipTable){
            for(const RetainedBufferStateCommit& commit : m_bufferStateCommits){
                if(commit.buffer.get() == &buffer){
                    updateCacheFlags(key, s_FlagOwned | s_FlagBuffer | s_FlagBufferStateCommit);
                    m_lastRetainedResource = key;
                    m_lastRetainedBuffer = &buffer;
                    return;
                }
            }
            m_bufferStateCommits.push_back(RetainedBufferStateCommit{ .buffer = MakeNotNull(&buffer) });
            updateCacheFlags(key, s_FlagOwned | s_FlagBuffer | s_FlagBufferStateCommit);
            m_lastRetainedResource = key;
            m_lastRetainedBuffer = &buffer;
            return;
        }
    }

    MembershipEntry& membership = findOrInsertMembership(key);
    if((membership.flags & s_FlagOwned) == 0u){
        m_resources.emplace_back(key, Handle<GraphicsResource>::deleter_type(&m_context.objectArena));
        membership.flags |= s_FlagOwned;
    }
    if((membership.flags & s_FlagBuffer) == 0u){
        pushRetainedBuffer(buffer);
        membership.flags |= s_FlagBuffer;
    }
    if((membership.flags & s_FlagBufferStateCommit) == 0u){
        m_bufferStateCommits.push_back(RetainedBufferStateCommit{ .buffer = MakeNotNull(&buffer) });
        membership.flags |= s_FlagBufferStateCommit;
    }
    updateCacheFlags(key, membership.flags);
    m_lastRetainedResource = key;
    m_lastRetainedBuffer = &buffer;
}

void CommandBufferResourceReferences::discardBufferStateCommits()noexcept{
    if(m_bufferStateCommits.empty())
        return;

    for(const RetainedBufferStateCommit& commit : m_bufferStateCommits){
        GraphicsResource* const key = commit.buffer.get();
        CacheSlot& slot = m_cache[CacheSlotIndex(key)];
        if(slot.resource == key && slot.generation == m_cacheGeneration)
            slot.flags = static_cast<u8>(slot.flags & ~s_FlagBufferStateCommit);
        if(m_usingMembershipTable){
            MembershipEntry* const found = findMembershipEntry(key);
            if(!found || (found->flags & s_FlagBufferStateCommit) == 0u)
                TerminateInvariant();
            found->flags = static_cast<u8>(found->flags & ~s_FlagBufferStateCommit);
        }
    }
    m_bufferStateCommits.clear();
}

void CommandBufferResourceReferences::retainTextureSlow(Texture& texture){
    GraphicsResource* const key = &texture;
    if(!m_usingMembershipTable){
        retainResource(texture);
        trackRetainedTextureSlow(texture);
        if(!m_usingMembershipTable){
            updateCacheFlags(key, s_FlagOwned | s_FlagTexture);
            m_lastRetainedResource = key;
            m_lastRetainedTexture = &texture;
            return;
        }
    }

    MembershipEntry& membership = findOrInsertMembership(key);
    if((membership.flags & s_FlagOwned) == 0u){
        m_resources.emplace_back(key, Handle<GraphicsResource>::deleter_type(&m_context.objectArena));
        membership.flags |= s_FlagOwned;
    }
    if((membership.flags & s_FlagTexture) == 0u){
        pushRetainedTexture(texture);
        membership.flags |= s_FlagTexture;
    }
    updateCacheFlags(key, membership.flags);
    m_lastRetainedResource = key;
    m_lastRetainedTexture = &texture;
}

void CommandBufferResourceReferences::trackRetainedTextureSlow(Texture& texture){
    using namespace __hidden_command_buffer_resource_references;
    GraphicsResource* const key = &texture;
    if(!m_usingMembershipTable){
        for(const auto retainedTexture : m_textures){
            if(retainedTexture.get() == &texture){
                updateCacheFlags(key, s_FlagTexture);
                return;
            }
        }
        if(m_textures.size() < s_LinearCapacity){
            pushRetainedTexture(texture);
            updateCacheFlags(key, s_FlagTexture);
            return;
        }
        promoteMembershipIndex();
    }

    MembershipEntry& membership = findOrInsertMembership(key);
    if((membership.flags & s_FlagTexture) == 0u){
        pushRetainedTexture(texture);
        membership.flags |= s_FlagTexture;
    }
    updateCacheFlags(key, membership.flags);
}

void CommandBufferResourceReferences::retainFramebufferSlow(Framebuffer& framebuffer){
    GraphicsResource* const key = &framebuffer;
    if(!m_usingMembershipTable){
        retainResource(framebuffer);
        if(!m_usingMembershipTable){
            const CacheSlot& slot = m_cache[CacheSlotIndex(key)];
            const b8 alreadyTracked = slot.resource == key && slot.generation == m_cacheGeneration && (slot.flags & s_FlagFramebuffer) != 0u;
            if(!alreadyTracked){
                for(const TextureHandle& texture : framebuffer.m_resources){
                    FOUNDATION_ASSERT(texture);
                    trackRetainedTexture(*texture);
                }
            }
            if(m_usingMembershipTable){
                MembershipEntry& promoted = findOrInsertMembership(key);
                promoted.flags |= static_cast<u8>(s_FlagOwned | s_FlagFramebuffer);
                updateCacheFlags(key, promoted.flags);
            }
            else
                updateCacheFlags(key, static_cast<u8>(s_FlagOwned | s_FlagFramebuffer));
            m_lastRetainedResource = key;
            m_lastRetainedFramebuffer = &framebuffer;
            return;
        }
    }

    MembershipEntry& membership = findOrInsertMembership(key);
    if((membership.flags & s_FlagOwned) == 0u){
        m_resources.emplace_back(key, Handle<GraphicsResource>::deleter_type(&m_context.objectArena));
        membership.flags |= s_FlagOwned;
    }
    const b8 needsAttachmentScan = (membership.flags & s_FlagFramebuffer) == 0u;
    if(needsAttachmentScan){
        membership.flags |= s_FlagFramebuffer;
        for(const TextureHandle& texture : framebuffer.m_resources){
            FOUNDATION_ASSERT(texture);
            trackRetainedTexture(*texture);
        }
    }
    MembershipEntry& refreshed = findOrInsertMembership(key);
    updateCacheFlags(key, refreshed.flags);
    m_lastRetainedResource = key;
    m_lastRetainedFramebuffer = &framebuffer;
}

void CommandBufferResourceReferences::clear()noexcept{
    m_lastRetainedResource = nullptr;
    m_lastRetainedBuffer = nullptr;
    m_lastRetainedTexture = nullptr;
    m_lastRetainedFramebuffer = nullptr;
    m_hasDynamicReadinessResources = false;
    m_hasCloseValidationTextures = false;
    m_hasCloseValidationBuffers = false;
    m_usingMembershipTable = false;
    m_membershipCount = 0u;
    m_bufferStateCommits.clear();
    m_buffers.clear();
    m_textures.clear();
    m_resources.clear();
    if(++m_cacheGeneration == 0u){
        m_cacheGeneration = 1u;
        m_cache.fill(CacheSlot{});
        for(MembershipEntry& entry : m_membershipTable)
            entry.generation = 0u;
    }
}

void CommandBufferResourceReferences::promoteMembershipIndex(){
    using namespace __hidden_command_buffer_resource_references;
    FOUNDATION_ASSERT(!m_usingMembershipTable);
    FOUNDATION_ASSERT(m_resources.size() <= s_LinearCapacity);
    FOUNDATION_ASSERT(m_buffers.size() <= s_LinearCapacity);
    FOUNDATION_ASSERT(m_textures.size() <= s_LinearCapacity);
    if(m_membershipTable.size() < s_InitialMembershipTableCapacity)
        m_membershipTable.resize(s_InitialMembershipTableCapacity);
    m_usingMembershipTable = true;
    m_membershipCount = 0u;
    for(const Handle<GraphicsResource>& resource : m_resources)
        findOrInsertMembership(resource.get()).flags |= s_FlagOwned;
    for(const auto buffer : m_buffers)
        findOrInsertMembership(buffer.get()).flags |= s_FlagBuffer;
    for(const auto texture : m_textures)
        findOrInsertMembership(texture.get()).flags |= s_FlagTexture;
    for(const RetainedBufferStateCommit& commit : m_bufferStateCommits)
        findOrInsertMembership(commit.buffer.get()).flags |= s_FlagBufferStateCommit;
    for(const CacheSlot& slot : m_cache){
        if(slot.generation == m_cacheGeneration && (slot.flags & s_FlagFramebuffer) != 0u && slot.resource)
            findOrInsertMembership(slot.resource).flags |= s_FlagFramebuffer;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

