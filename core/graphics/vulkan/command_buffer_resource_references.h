// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RetainedBufferStateCommit{
    Buffer* buffer = nullptr;
};

class CommandBufferResourceReferences final : NoCopy{
    friend class CommandList;
    friend class TrackedCommandBuffer;
    friend class VulkanTestDispatchAccess;


private:
    using MembershipIndex = HashMap<GraphicsResource*, u8, Alloc::GlobalArena>;


public:
    explicit CommandBufferResourceReferences(Alloc::GlobalArena& arena);
    ~CommandBufferResourceReferences() = default;


public:
    void retainResource(GraphicsResource& resource);
    void retainBuffer(Buffer& buffer);
    void trackRetainedBuffer(Buffer& buffer);
    void appendBufferStateCommit(Buffer& buffer);
    void discardBufferStateCommits()noexcept;
    void retainTexture(Texture& texture);
    void trackRetainedTexture(Texture& texture);
    void clear()noexcept;


private:
    void promoteMembershipIndex();


private:
    Vector<Handle<GraphicsResource>, Alloc::GlobalArena> m_resources;
    Vector<Buffer*, Alloc::GlobalArena> m_buffers;
    Vector<Texture*, Alloc::GlobalArena> m_textures;
    Vector<RetainedBufferStateCommit, Alloc::GlobalArena> m_bufferStateCommits;
    Optional<MembershipIndex> m_membership;
    Alloc::GlobalArena& m_arena;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

