// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/graphics/rhi/command.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Accepted native snapshot between graph generations, for live imports used in a later graph.
// Retains typed handles so rebuilds cannot dangle; no renderer policy leaks into this utility.
class GpuPersistentResourceStateCache final : NoCopy{
public:
    // A pre-submission candidate built for one cache from a recorded packet's final state.
    // On commit, the cache exchanges storage with it.
    // The consumed candidate keeps the displaced accepted storage alive until its caller has unwound outside the accepted-submission gate.
    class Candidate final : NoCopy{
        friend class GpuPersistentResourceStateCache;


    public:
        explicit Candidate(const GpuPersistentResourceStateCache& owner)noexcept
            : m_owner(owner)
            , m_states(owner.m_arena)
            , m_textures(owner.m_arena)
            , m_buffers(owner.m_arena)
        {}
        Candidate(const GpuPersistentResourceStateCache&&) = delete;


    public:
        [[nodiscard]] b8 valid()const noexcept{ return !m_consumed && m_states.valid(); }

        [[nodiscard]] b8 empty()const noexcept{ return !valid() || m_states.empty(); }

        [[nodiscard]] const CommandListResourceStateHandoff* source()const noexcept{ return valid() ? &m_states : nullptr; }


    private:
        void reset()noexcept;


    private:
        const GpuPersistentResourceStateCache& m_owner;
        CommandListResourceStateHandoff m_states;
        GraphicsVector<TextureHandle> m_textures;
        GraphicsVector<BufferHandle> m_buffers;
        b8 m_consumed = false;
    };


public:
    explicit GpuPersistentResourceStateCache(GraphicsArena& arena)noexcept
        : m_arena(arena)
        , m_states(arena)
        , m_textures(arena)
        , m_buffers(arena)
        , m_spareTextures(arena)
        , m_spareBuffers(arena)
    {}
    GpuPersistentResourceStateCache(GpuPersistentResourceStateCache&&) = delete;


public:
    void reset()noexcept;

    [[nodiscard]] b8 valid()const noexcept{ return m_states.valid(); }

    [[nodiscard]] b8 empty()const noexcept{ return !m_states.valid() || m_states.empty(); }

    [[nodiscard]] const CommandListResourceStateHandoff* source()const noexcept{ return m_states.valid() ? &m_states : nullptr; }

    // Replaces the cache with `source` filtered to live imports, for pruning a removed runtime generation.
    [[nodiscard]] b8 replaceResourceSubset(const CommandListResourceStateHandoff& source, const TextureHandle* textures, usize textureCount, const BufferHandle* buffers, usize bufferCount, Alloc::ScratchArena& scratchArena);

    // Builds an acceptance-only candidate from the retained base state and `source`.
    // The caller must call commit() only after the packet that produced `source` is accepted; a rejected packet leaves this cache untouched.
    [[nodiscard]] b8 buildMergedResourceSubset(Candidate& outCandidate, const CommandListResourceStateHandoff& source, const TextureHandle* textures, usize textureCount, const BufferHandle* buffers, usize bufferCount, Alloc::ScratchArena& scratchArena)const;

    // Produces a typed, filtered source for recording without altering the accepted cache.
    // This preserves the last accepted state when the newly recorded packet is later rejected.
    [[nodiscard]] b8 buildFilteredResourceSubset(
        Candidate& outCandidate,
        const CommandListResourceStateHandoff& source,
        const TextureHandle* textures,
        usize textureCount,
        const BufferHandle* buffers,
        usize bufferCount,
        Alloc::ScratchArena& scratchArena
    )const;

    [[nodiscard]] b8 commit(Candidate& candidate)noexcept;

    [[nodiscard]] b8 replaceBufferSubset(const CommandListResourceStateHandoff& source, const BufferHandle* buffers, const usize bufferCount, Alloc::ScratchArena& scratchArena){
        return replaceResourceSubset(source, nullptr, 0u, buffers, bufferCount, scratchArena);
    }

    [[nodiscard]] b8 buildMergedBufferSubset(Candidate& outCandidate, const CommandListResourceStateHandoff& source, const BufferHandle* buffers, const usize bufferCount, Alloc::ScratchArena& scratchArena)const{
        return buildMergedResourceSubset(outCandidate, source, nullptr, 0u, buffers, bufferCount, scratchArena);
    }

    [[nodiscard]] b8 buildFilteredBufferSubset(Candidate& outCandidate, const CommandListResourceStateHandoff& source, const BufferHandle* buffers, const usize bufferCount, Alloc::ScratchArena& scratchArena)const{
        return buildFilteredResourceSubset(outCandidate, source, nullptr, 0u, buffers, bufferCount, scratchArena);
    }


private:
    GraphicsArena& m_arena;
    CommandListResourceStateHandoff m_states;
    GraphicsVector<TextureHandle> m_textures;
    GraphicsVector<BufferHandle> m_buffers;
    // The in-place self filter stages new handles here and swaps them in. These stay empty between calls and only keep capacity.
    GraphicsVector<TextureHandle> m_spareTextures;
    GraphicsVector<BufferHandle> m_spareBuffers;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

