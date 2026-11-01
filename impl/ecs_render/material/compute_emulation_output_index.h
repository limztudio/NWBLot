// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/shared/frame_types.adhd.h>

#include <core/graphics/rhi/resource.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Per-operation emulation-output membership; buffers and slots are separate domains.
template<typename Identity>
class MaterialPassEmulationOutputIndex final : NoCopy{
private:
    using Index = HashSet<Identity, Core::Alloc::ScratchArena>;
    using InlineIdentity = Conditional_T<IsSame_V<Identity, u32>, u32, Core::Buffer*>;


private:
    static_assert(IsSame_V<Identity, NotNull<Core::Buffer*>> || IsSame_V<Identity, u32>);


public:
    explicit MaterialPassEmulationOutputIndex(Core::Alloc::ScratchArena& scratchArena)noexcept
        : m_scratchArena(scratchArena)
    {}
    MaterialPassEmulationOutputIndex(MaterialPassEmulationOutputIndex&&) = delete;


public:
    [[nodiscard]] b8 insert(const Identity identity){
        if(m_index)
            return m_index->insert(identity).second;
        if(contains(identity))
            return false;
        appendNew(identity);
        return true;
    }

    // Seeds and snapshots permit repeated identities without changing owners.
    void include(const Identity identity){
        if(m_index){
            m_index->insert(identity);
            return;
        }
        if(!contains(identity))
            appendNew(identity);
    }

    [[nodiscard]] b8 contains(const Identity identity)const noexcept{
        if(m_index)
            return m_index->find(identity) != m_index->end();
        for(usize index = 0u; index < m_inlineCount; ++index){
            if constexpr(IsSame_V<Identity, u32>){
                if(m_inline[index] == identity)
                    return true;
            }
            else if(m_inline[index] == identity.get())
                return true;
        }
        return false;
    }


private:
    void appendNew(const Identity identity){
        if(m_inlineCount < s_SmallInlineIndexCapacity){
            if constexpr(IsSame_V<Identity, u32>)
                m_inline[m_inlineCount] = identity;
            else
                m_inline[m_inlineCount] = identity.get();
            ++m_inlineCount;
            return;
        }
        Index index(s_PromotedInlineIndexCapacity, m_scratchArena);
        for(const InlineIdentity stored : m_inline){
            const Identity existing = [&]()noexcept{
                if constexpr(IsSame_V<Identity, u32>)
                    return stored;
                else
                    return MakeNotNull(stored);
            }();
            if(!index.insert(existing).second)
                TerminateInvariant();
        }
        if(!index.insert(identity).second)
            TerminateInvariant();
        static_assert(IsNothrowMoveConstructible_V<Index>);
        m_index.emplace(Move(index));
    }


private:
    Core::Alloc::ScratchArena& m_scratchArena;
    Array<InlineIdentity, s_SmallInlineIndexCapacity> m_inline{};
    usize m_inlineCount = 0u;
    Optional<Index> m_index;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

