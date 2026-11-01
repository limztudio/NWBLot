// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "live_state_buffers.h"

#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void MeshSkinningStateBufferCollector::collect(const MeshSkinningRuntimeInstance& instance, const MeshSkinningStateBufferResources& resources){
    if(!instance.valid())
        return;

    for(const Core::BufferHandle& buffer : CaptureMeshSkinningResourceBuffers(instance))
        retainBuffer(buffer);

    if(resources.editRevision == instance.editRevision){
        if(resources.skinBuffer)
            retainBuffer(*resources.skinBuffer);
        if(resources.jointPaletteBuffer)
            retainBuffer(*resources.jointPaletteBuffer);
        if(resources.bindlessResourceSlotsBuffer)
            retainBuffer(*resources.bindlessResourceSlotsBuffer);
    }
}

void MeshSkinningStateBufferCollector::retainBuffer(const Core::BufferHandle& buffer){
    Core::Buffer* const identity = buffer.get();
    if(!identity)
        return;
    if(!m_index){
        for(usize index = 0u; index < m_inlineCount; ++index){
            if(m_inlineBuffers[index] == identity)
                return;
        }
        if(m_inlineCount < s_SkinningLiveStateInlineBufferCount){
            m_buffers.push_back(buffer);
            m_inlineBuffers[m_inlineCount] = identity;
            ++m_inlineCount;
            return;
        }
        promoteBufferIndex();
    }
    const auto [found, inserted] = m_index->insert(MakeNotNull(identity));
    if(!inserted)
        return;
    ScopeExit discardIdentity([&]()noexcept{ m_index->erase(found); });

    m_buffers.push_back(buffer);
    discardIdentity.release();
}

void MeshSkinningStateBufferCollector::promoteBufferIndex(){
    NWB_ASSERT(!m_index && m_inlineCount == s_SkinningLiveStateInlineBufferCount && m_buffers.size() == m_inlineCount);
    BufferIndex index(s_SkinningLiveStateInlineBufferCount * s_SkinningPromotedIndexCapacityFactor, m_scratchArena);
    for(Core::Buffer* const identity : m_inlineBuffers){
        const auto [found, inserted] = index.insert(MakeNotNull(identity));
        if(!inserted)
            TerminateInvariant();
    }
    static_assert(IsNothrowMoveConstructible_V<BufferIndex>);
    m_index.emplace(Move(index));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

