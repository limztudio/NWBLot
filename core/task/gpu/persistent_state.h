// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/graphics/rhi/command.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuPersistentResourceStateCache final : NoCopy{
public:
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

        [[nodiscard]] Optional<NotNull<const CommandListResourceStateHandoff*>> borrowSource()const noexcept{
            if(valid())
                return MakeNotNull(&m_states);
            return {};
        }


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

    [[nodiscard]] Optional<NotNull<const CommandListResourceStateHandoff*>> borrowSource()const noexcept{
        if(valid())
            return MakeNotNull(&m_states);
        return {};
    }

    [[nodiscard]] b8 replaceBufferSubset(const CommandListResourceStateHandoff& source, Span<const BufferHandle> buffers);

    [[nodiscard]] b8 replaceResourceSubset(const CommandListResourceStateHandoff& source, Span<const TextureHandle> textures, Span<const BufferHandle> buffers, Alloc::ScratchArena& scratchArena);

    [[nodiscard]] b8 buildMergedResourceSubset(const CommandListResourceStateHandoff& source, Span<const TextureHandle> textures, Span<const BufferHandle> buffers, Alloc::ScratchArena& scratchArena, Candidate& outCandidate)const;

    [[nodiscard]] b8 buildFilteredResourceSubset(
        const CommandListResourceStateHandoff& source,
        Span<const TextureHandle> textures,
        Span<const BufferHandle> buffers,
        Alloc::ScratchArena& scratchArena,
        Candidate& outCandidate
    )const;

    [[nodiscard]] b8 buildFilteredResourceSubset(const CommandListResourceStateHandoff& source, Span<const TextureHandle> textures, Span<const BufferHandle> buffers, Candidate& candidate)const;
    [[nodiscard]] b8 buildMergedResourceSubset(const CommandListResourceStateHandoff& source, Span<const TextureHandle> textures, Span<const BufferHandle> buffers, Candidate& candidate)const;

    [[nodiscard]] b8 commit(Candidate& candidate)noexcept;

    [[nodiscard]] b8 replaceBufferSubset(const CommandListResourceStateHandoff& source, const Span<const BufferHandle> buffers, Alloc::ScratchArena& scratchArena){
        return replaceResourceSubset(source, {}, buffers, scratchArena);
    }

    [[nodiscard]] b8 buildMergedBufferSubset(const CommandListResourceStateHandoff& source, const Span<const BufferHandle> buffers, Alloc::ScratchArena& scratchArena, Candidate& outCandidate)const{
        return buildMergedResourceSubset(source, {}, buffers, scratchArena, outCandidate);
    }

    [[nodiscard]] b8 buildMergedBufferSubset(const CommandListResourceStateHandoff& source, Span<const BufferHandle> buffers, Candidate& candidate)const;


private:
    GraphicsArena& m_arena;
    CommandListResourceStateHandoff m_states;
    GraphicsVector<TextureHandle> m_textures;
    GraphicsVector<BufferHandle> m_buffers;
    GraphicsVector<TextureHandle> m_spareTextures;
    GraphicsVector<BufferHandle> m_spareBuffers;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

