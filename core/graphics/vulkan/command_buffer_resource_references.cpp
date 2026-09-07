// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_buffer_resource_references.h"

#include "backend.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_command_buffer_resource_references{


inline constexpr usize s_LinearCapacity = 32u;
inline constexpr u8 s_Owned = 1u << 0u;
inline constexpr u8 s_Buffer = 1u << 1u;
inline constexpr u8 s_Texture = 1u << 2u;
inline constexpr u8 s_BufferStateCommit = 1u << 3u;


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CommandBufferResourceReferences::CommandBufferResourceReferences(Alloc::GlobalArena& arena)
    : m_resources(arena)
    , m_buffers(arena)
    , m_textures(arena)
    , m_bufferStateCommits(arena)
    , m_arena(arena)
{}

void CommandBufferResourceReferences::retainResource(GraphicsResource& resource){
    using namespace __hidden_command_buffer_resource_references;
    if(!m_membership){
        for(const Handle<GraphicsResource>& retainedResource : m_resources){
            if(retainedResource.get() == &resource)
                return;
        }
        if(m_resources.size() < s_LinearCapacity){
            m_resources.emplace_back(&resource, Handle<GraphicsResource>::deleter_type(&m_arena));
            return;
        }
        promoteMembershipIndex();
    }

    u8& membership = m_membership->try_emplace(&resource, 0u).first.value();
    if((membership & s_Owned) != 0u)
        return;
    m_resources.emplace_back(&resource, Handle<GraphicsResource>::deleter_type(&m_arena));
    membership |= s_Owned;
}

void CommandBufferResourceReferences::retainBuffer(Buffer& buffer){
    retainResource(buffer);
    trackRetainedBuffer(buffer);
}

void CommandBufferResourceReferences::trackRetainedBuffer(Buffer& buffer){
    using namespace __hidden_command_buffer_resource_references;
    if(!m_membership){
        for(Buffer* const retainedBuffer : m_buffers){
            if(retainedBuffer == &buffer)
                return;
        }
        if(m_buffers.size() < s_LinearCapacity){
            m_buffers.push_back(&buffer);
            return;
        }
        promoteMembershipIndex();
    }

    u8& membership = m_membership->try_emplace(&buffer, 0u).first.value();
    if((membership & s_Buffer) != 0u)
        return;
    m_buffers.push_back(&buffer);
    membership |= s_Buffer;
}

void CommandBufferResourceReferences::appendBufferStateCommit(Buffer& buffer){
    using namespace __hidden_command_buffer_resource_references;
    retainBuffer(buffer);
    if(!m_membership){
        for(const RetainedBufferStateCommit& commit : m_bufferStateCommits){
            if(commit.buffer == &buffer)
                return;
        }
        m_bufferStateCommits.push_back(RetainedBufferStateCommit{ .buffer = &buffer });
        return;
    }

    const auto found = m_membership->find(&buffer);
    NWB_ASSERT(found != m_membership->end());
    u8& membership = found.value();
    if((membership & s_BufferStateCommit) != 0u)
        return;
    m_bufferStateCommits.push_back(RetainedBufferStateCommit{ .buffer = &buffer });
    membership |= s_BufferStateCommit;
}

void CommandBufferResourceReferences::discardBufferStateCommits()noexcept{
    using namespace __hidden_command_buffer_resource_references;
    if(m_membership){
        for(const RetainedBufferStateCommit& commit : m_bufferStateCommits){
            const auto found = m_membership->find(commit.buffer);
            if(found == m_membership->end() || (found.value() & s_BufferStateCommit) == 0u)
                TerminateInvariant();
            found.value() &= static_cast<u8>(~s_BufferStateCommit);
        }
    }
    m_bufferStateCommits.clear();
}

void CommandBufferResourceReferences::retainTexture(Texture& texture){
    retainResource(texture);
    trackRetainedTexture(texture);
}

void CommandBufferResourceReferences::trackRetainedTexture(Texture& texture){
    using namespace __hidden_command_buffer_resource_references;
    if(!m_membership){
        for(Texture* const retainedTexture : m_textures){
            if(retainedTexture == &texture)
                return;
        }
        if(m_textures.size() < s_LinearCapacity){
            m_textures.push_back(&texture);
            return;
        }
        promoteMembershipIndex();
    }

    u8& membership = m_membership->try_emplace(&texture, 0u).first.value();
    if((membership & s_Texture) != 0u)
        return;
    m_textures.push_back(&texture);
    membership |= s_Texture;
}

void CommandBufferResourceReferences::clear()noexcept{
    m_bufferStateCommits.clear();
    if(m_membership)
        m_membership->clear();
    m_buffers.clear();
    m_textures.clear();
    m_resources.clear();
}

void CommandBufferResourceReferences::promoteMembershipIndex(){
    using namespace __hidden_command_buffer_resource_references;
    NWB_ASSERT(!m_membership);
    NWB_ASSERT(m_resources.size() <= s_LinearCapacity);
    NWB_ASSERT(m_buffers.size() <= s_LinearCapacity);
    NWB_ASSERT(m_textures.size() <= s_LinearCapacity);
    const usize maximumEntries = m_resources.size() + m_buffers.size() + m_textures.size();
    MembershipIndex membership(maximumEntries * 2u, MembershipIndex::allocator_type(m_arena));
    for(const Handle<GraphicsResource>& resource : m_resources)
        membership[resource.get()] |= s_Owned;
    for(Buffer* const buffer : m_buffers)
        membership[buffer] |= s_Buffer;
    for(Texture* const texture : m_textures)
        membership[texture] |= s_Texture;
    for(const RetainedBufferStateCommit& commit : m_bufferStateCommits)
        membership[commit.buffer] |= s_BufferStateCommit;

    // Build the complete membership off to the side. Allocation failure leaves the ordered lists authoritative;
    // after publication every lookup uses this index, including non-owning and pending-publication membership.
    static_assert(IsNothrowMoveConstructible_V<MembershipIndex>);
    m_membership.emplace(Move(membership));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

