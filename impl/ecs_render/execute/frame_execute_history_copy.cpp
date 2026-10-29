// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_execute_history_copy.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 FrameExecuteHistoryCopy::PrepareHistoryCopyFinalState(void* const rawContext, const Core::CommandListResourceStateHandoff* const finalState){
    FrameExecuteHistoryCopy::Context* const context = static_cast<Context*>(rawContext);
    if(!context || !finalState)
        return false;

    const b8 shadowStateReady = context->renderer->m_shadowVisibilityReturnState.buildFilteredResourceSubset(*context->shadowStateCandidate, *finalState, &context->targets->shadowVisibility, 1u, nullptr, 0u, context->scratchArena);
    const b8 causticStateReady = context->renderer->m_causticIrradianceReturnState.buildFilteredResourceSubset(*context->causticStateCandidate, *finalState, &context->targets->causticIrradiance, 1u, nullptr, 0u, context->scratchArena);
    const b8 surfelStateReady = context->renderer->m_surfelIrradianceReturnState.buildFilteredResourceSubset(*context->surfelStateCandidate, *finalState, &context->targets->surfelIrradiance, 1u, nullptr, 0u, context->scratchArena);
    context->finalStateReady = shadowStateReady && causticStateReady && surfelStateReady;
    return context->finalStateReady;
}


[[nodiscard]] b8 FrameExecuteHistoryCopy::AcceptHistoryCopyFinalState(void* const rawContext, const Core::QueueSubmissionToken& token)noexcept{
    static_cast<void>(token);
    FrameExecuteHistoryCopy::Context* const context = static_cast<Context*>(rawContext);
    if(!context || !context->finalStateReady)
        return false;

    const b8 shadowStateReady = context->renderer->m_shadowVisibilityReturnState.commit(*context->shadowStateCandidate);
    const b8 causticStateReady = context->renderer->m_causticIrradianceReturnState.commit(*context->causticStateCandidate);
    const b8 surfelStateReady = context->renderer->m_surfelIrradianceReturnState.commit(*context->surfelStateCandidate);
    context->acceptedStateReady = shadowStateReady && causticStateReady && surfelStateReady;
    return context->acceptedStateReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

