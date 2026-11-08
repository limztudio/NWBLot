// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skinning_state_handoff.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SkinningStateHandoff::SkinningStateHandoff(Core::GpuPersistentResourceStateCache& cache, Core::Alloc::ScratchArena& scratchArena, const Span<const Core::BufferHandle> buffers)
    : m_cache(cache)
    , m_scratchArena(scratchArena)
    , m_candidate(cache)
    , m_buffers(buffers)
{}

b8 SkinningStateHandoff::Prepare(void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState){
    SkinningStateHandoff* const context = static_cast<SkinningStateHandoff*>(rawContext);
    if(!context)
        return false;
    context->m_preparationInvoked = true;
    if(!finalState || (!context->m_buffers.empty() && !context->m_buffers.data()))
        return false;
    context->m_statePrepared = context->m_cache.buildMergedBufferSubset(*finalState, Span<const Core::BufferHandle>(context->m_buffers), context->m_scratchArena, context->m_candidate);
    return context->m_statePrepared;
}

b8 SkinningStateHandoff::Accept(void* const rawContext, [[maybe_unused]] const Core::QueueSubmissionToken& token)noexcept{
    SkinningStateHandoff* const context = static_cast<SkinningStateHandoff*>(rawContext);
    if(!context || !context->m_statePrepared)
        return false;
    context->m_stateAccepted = context->m_cache.commit(context->m_candidate);
    return context->m_stateAccepted;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

