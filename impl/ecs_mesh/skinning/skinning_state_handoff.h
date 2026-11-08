// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/alloc/scratch.h>
#include <core/task/gpu/persistent_state.h>
#include <core/task/gpu/scheduler.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SkinningStateHandoff final : NoCopy{
public:
    SkinningStateHandoff(Core::GpuPersistentResourceStateCache& cache, Core::Alloc::ScratchArena& scratchArena, Span<const Core::BufferHandle> buffers);


public:
    [[nodiscard]] static b8 Prepare(void* context, const Core::CommandListResourceStateHandoff* finalState);
    [[nodiscard]] static b8 Accept(void* context, const Core::QueueSubmissionToken& token)noexcept;
    [[nodiscard]] b8 preparationInvoked()const noexcept{ return m_preparationInvoked; }
    [[nodiscard]] b8 statePrepared()const noexcept{ return m_statePrepared; }
    [[nodiscard]] b8 stateAccepted()const noexcept{ return m_stateAccepted; }


private:
    Core::GpuPersistentResourceStateCache& m_cache;
    Core::Alloc::ScratchArena& m_scratchArena;
    Core::GpuPersistentResourceStateCache::Candidate m_candidate;
    Span<const Core::BufferHandle> m_buffers;
    b8 m_preparationInvoked = false;
    b8 m_statePrepared = false;
    b8 m_stateAccepted = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

