// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/graphics/rhi/resource.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Per-operation membership for compute-emulation output ownership. Buffers and descriptor slots are separate
// identity domains; the plans choose which domains participate in their existing alias contract.
template<typename Identity>
class MaterialPassEmulationOutputIndex final : NoCopy{
private:
    using Index = HashSet<Identity, Core::Alloc::ScratchArena>;
    static_assert(IsSame_V<Identity, Core::Buffer*> || IsSame_V<Identity, u32>);


private:
    static constexpr usize s_InlineCount = 32u;


public:
    explicit MaterialPassEmulationOutputIndex(Core::Alloc::ScratchArena& scratchArena)
        : m_scratchArena(scratchArena)
    {}
    MaterialPassEmulationOutputIndex(MaterialPassEmulationOutputIndex&&) = delete;


public:
    [[nodiscard]] bool insert(const Identity identity){
        if(m_index)
            return m_index->insert(identity).second;
        if(contains(identity))
            return false;
        appendNew(identity);
        return true;
    }

    // Seeds and mutable receiver snapshots permit repeated identities without changing their owning row arrays.
    void include(const Identity identity){
        if(m_index){
            m_index->insert(identity);
            return;
        }
        if(!contains(identity))
            appendNew(identity);
    }

    [[nodiscard]] bool contains(const Identity identity)const{
        if(m_index)
            return m_index->find(identity) != m_index->end();
        for(usize index = 0u; index < m_inlineCount; ++index){
            if(m_inline[index] == identity)
                return true;
        }
        return false;
    }


private:
    void appendNew(const Identity identity){
        if(m_inlineCount < s_InlineCount){
            m_inline[m_inlineCount] = identity;
            ++m_inlineCount;
            return;
        }
        Index index(s_InlineCount * 4u, m_scratchArena);
        for(const Identity existing : m_inline)
            index.insert(existing);
        index.insert(identity);
        static_assert(IsNothrowMoveConstructible_V<Index>);
        m_index.emplace(Move(index));
    }


private:
    Core::Alloc::ScratchArena& m_scratchArena;
    Identity m_inline[s_InlineCount];
    usize m_inlineCount = 0u;
    Optional<Index> m_index;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

