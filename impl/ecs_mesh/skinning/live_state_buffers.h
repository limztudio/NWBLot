// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "runtime_instance.h"

#include <core/alloc/scratch.h>

#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Borrows the optional runtime resources resolved for this instance's complete runtime handle.
struct MeshSkinningStateBufferResources{
    u32 editRevision = 0u;
    const Core::BufferHandle* skinBuffer = nullptr;
    const Core::BufferHandle* jointPaletteBuffer = nullptr;
    const Core::BufferHandle* bindlessResourceSlotsBuffer = nullptr;
};

// One collection visits live bindings in world order; collector retains no state.
class MeshSkinningStateBufferCollector final : NoCopy{
private:
    using BufferIndex = HashSet<NotNull<Core::Buffer*>, Core::Alloc::ScratchArena>;


public:
    MeshSkinningStateBufferCollector(Core::Alloc::ScratchArena& scratchArena, Vector<Core::BufferHandle, Core::Alloc::GlobalArena>& outBuffers)noexcept
        : m_buffers(outBuffers)
        , m_scratchArena(scratchArena)
    {
        m_buffers.clear();
    }


public:
    void collect(const MeshSkinningRuntimeInstance& instance, const MeshSkinningStateBufferResources& resources){
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


private:
    void retainBuffer(const Core::BufferHandle& buffer){
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
    void promoteBufferIndex(){
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


private:
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena>& m_buffers;
    Core::Alloc::ScratchArena& m_scratchArena;
    Array<Core::Buffer*, s_SkinningLiveStateInlineBufferCount> m_inlineBuffers{};
    usize m_inlineCount = 0u;
    Optional<BufferIndex> m_index;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

