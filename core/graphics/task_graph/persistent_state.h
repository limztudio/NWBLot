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
    // A pre-submission candidate built from a recorded packet's final state. It retains its own typed backings until
    // the containing packet is accepted, at which point the cache atomically adopts it through commit().
    class Candidate final : NoCopy{
        friend class GpuPersistentResourceStateCache;

    public:
        explicit Candidate(GraphicsArena& arena)
            : m_states(arena)
            , m_textures(arena)
            , m_buffers(arena)
        {}


    public:
        [[nodiscard]] bool valid()const noexcept{ return m_states.valid(); }
        [[nodiscard]] bool empty()const noexcept{ return !m_states.valid() || m_states.empty(); }
        [[nodiscard]] const CommandListResourceStateHandoff* source()const noexcept{
            return m_states.valid() ? &m_states : nullptr;
        }


    private:
        void reset()noexcept;


    private:
        CommandListResourceStateHandoff m_states;
        GraphicsVector<TextureHandle> m_textures;
        GraphicsVector<BufferHandle> m_buffers;
    };

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

    // Builds an acceptance-only candidate from the retained base state and `source`. The caller must call commit()
    // only after the packet that produced `source` is accepted; a rejected packet leaves this cache untouched.
    [[nodiscard]] bool buildMergedResourceSubset(
        Candidate& outCandidate,
        const CommandListResourceStateHandoff& source,
        const TextureHandle* textures,
        usize textureCount,
        const BufferHandle* buffers,
        usize bufferCount
    )const;

    // Produces a typed, filtered source for recording without altering the accepted cache. This preserves the last
    // accepted state when the newly recorded packet is later rejected.
    [[nodiscard]] bool buildFilteredResourceSubset(
        Candidate& outCandidate,
        const CommandListResourceStateHandoff& source,
        const TextureHandle* textures,
        usize textureCount,
        const BufferHandle* buffers,
        usize bufferCount
    )const;

    [[nodiscard]] bool commit(Candidate& candidate);

    [[nodiscard]] bool replaceBufferSubset(
        const CommandListResourceStateHandoff& source,
        const BufferHandle* buffers,
        const usize bufferCount
    ){
        return replaceResourceSubset(source, nullptr, 0u, buffers, bufferCount);
    }

    [[nodiscard]] bool replaceTextureSubset(
        const CommandListResourceStateHandoff& source,
        const TextureHandle* textures,
        const usize textureCount
    ){
        return replaceResourceSubset(source, textures, textureCount, nullptr, 0u);
    }

    [[nodiscard]] bool replaceTextureSubset(
        const CommandListResourceStateHandoff& source,
        const TextureHandle& texture
    ){
        return replaceTextureSubset(source, &texture, 1u);
    }

    [[nodiscard]] bool mergeBufferSubset(
        const CommandListResourceStateHandoff& source,
        const BufferHandle* buffers,
        const usize bufferCount
    ){
        return mergeResourceSubset(source, nullptr, 0u, buffers, bufferCount);
    }

    [[nodiscard]] bool buildMergedBufferSubset(
        Candidate& outCandidate,
        const CommandListResourceStateHandoff& source,
        const BufferHandle* buffers,
        const usize bufferCount
    )const{
        return buildMergedResourceSubset(outCandidate, source, nullptr, 0u, buffers, bufferCount);
    }

    [[nodiscard]] bool buildFilteredBufferSubset(
        Candidate& outCandidate,
        const CommandListResourceStateHandoff& source,
        const BufferHandle* buffers,
        const usize bufferCount
    )const{
        return buildFilteredResourceSubset(outCandidate, source, nullptr, 0u, buffers, bufferCount);
    }


private:
    [[nodiscard]] bool buildFilteredCandidate(
        Candidate& outCandidate,
        const CommandListResourceStateHandoff& source,
        const TextureHandle* textures,
        usize textureCount,
        const BufferHandle* buffers,
        usize bufferCount
    )const;


private:
    GraphicsArena& m_arena;
    CommandListResourceStateHandoff m_states;
    GraphicsVector<TextureHandle> m_textures;
    GraphicsVector<BufferHandle> m_buffers;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
