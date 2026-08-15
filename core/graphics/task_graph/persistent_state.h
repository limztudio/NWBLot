// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/graphics/rhi/command.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns an accepted, filtered native state snapshot between graph generations.  The graph still decides every
// in-frame transition; this cache exists only for live imported resources whose next use occurs in a later graph.
// It retains the typed resource handles alongside the raw native snapshot, so pruning/rebuilding a resource cannot
// leave an external graph state source dangling.  Callers provide their current live handles deliberately: no
// renderer-specific packet or state-tracker policy escapes into this utility.
class GpuPersistentResourceStateCache final : NoCopy{
public:
    explicit GpuPersistentResourceStateCache(GraphicsArena& arena)
        : m_arena(arena)
        , m_states(arena)
        , m_textures(arena)
        , m_buffers(arena)
    {}


public:
    void reset()noexcept;

    [[nodiscard]] bool valid()const noexcept{ return m_states.valid(); }
    [[nodiscard]] bool empty()const noexcept{ return !m_states.valid() || m_states.empty(); }
    [[nodiscard]] const CommandListResourceStateHandoff* source()const noexcept{
        return m_states.valid() ? &m_states : nullptr;
    }
    [[nodiscard]] usize retainedTextureCount()const noexcept{ return m_textures.size(); }
    [[nodiscard]] usize retainedBufferCount()const noexcept{ return m_buffers.size(); }

    // Replaces the accepted cache with `source` filtered to the supplied live imported resources.  This is useful
    // when resource pruning removes a runtime generation that the prior snapshot referenced.
    [[nodiscard]] bool replaceResourceSubset(
        const CommandListResourceStateHandoff& source,
        const TextureHandle* textures,
        usize textureCount,
        const BufferHandle* buffers,
        usize bufferCount
    );

    // Folds a graph packet's final native state into the retained accepted snapshot, then filters the result to the
    // caller's current live resources.  The retained cache is changed only after the new candidate is complete.
    [[nodiscard]] bool mergeResourceSubset(
        const CommandListResourceStateHandoff& source,
        const TextureHandle* textures,
        usize textureCount,
        const BufferHandle* buffers,
        usize bufferCount
    );

    [[nodiscard]] bool replaceBufferSubset(
        const CommandListResourceStateHandoff& source,
        const BufferHandle* buffers,
        const usize bufferCount
    ){
        return replaceResourceSubset(source, nullptr, 0u, buffers, bufferCount);
    }

    [[nodiscard]] bool mergeBufferSubset(
        const CommandListResourceStateHandoff& source,
        const BufferHandle* buffers,
        const usize bufferCount
    ){
        return mergeResourceSubset(source, nullptr, 0u, buffers, bufferCount);
    }


private:
    [[nodiscard]] bool buildFilteredCandidate(
        CommandListResourceStateHandoff& outStates,
        GraphicsVector<TextureHandle>& outTextures,
        GraphicsVector<BufferHandle>& outBuffers,
        const CommandListResourceStateHandoff& source,
        const TextureHandle* textures,
        usize textureCount,
        const BufferHandle* buffers,
        usize bufferCount
    )const;
    [[nodiscard]] bool commitCandidate(
        const CommandListResourceStateHandoff& states,
        GraphicsVector<TextureHandle>& textures,
        GraphicsVector<BufferHandle>& buffers
    );


private:
    GraphicsArena& m_arena;
    CommandListResourceStateHandoff m_states;
    GraphicsVector<TextureHandle> m_textures;
    GraphicsVector<BufferHandle> m_buffers;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
