// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/execute/frame_execute_history_copy.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool FrameExecuteHistoryCopy::PrepareHistoryCopyFinalState(
    void* const rawContext,
    const Core::CommandListResourceStateHandoff* const finalState
){
    FrameExecuteHistoryCopy::Context* const context =
        static_cast<Context*>(rawContext)
    ;
    if(
        !context
        || !context->renderer
        || !context->targets
        || !context->shadowStateCandidate
        || !context->causticStateCandidate
        || !context->surfelStateCandidate
        || !finalState
    )
        return false;

    const bool shadowStateReady =
        context->renderer->m_shadowVisibilityReturnState.buildFilteredResourceSubset(
            *context->shadowStateCandidate,
            *finalState,
            &context->targets->shadowVisibility,
            1u,
            nullptr,
            0u,
            context->scratchArena
        )
    ;
    const bool causticStateReady =
        context->renderer->m_causticIrradianceReturnState.buildFilteredResourceSubset(
            *context->causticStateCandidate,
            *finalState,
            &context->targets->causticIrradiance,
            1u,
            nullptr,
            0u,
            context->scratchArena
        )
    ;
    const bool surfelStateReady =
        context->renderer->m_surfelIrradianceReturnState.buildFilteredResourceSubset(
            *context->surfelStateCandidate,
            *finalState,
            &context->targets->surfelIrradiance,
            1u,
            nullptr,
            0u,
            context->scratchArena
        )
    ;
    context->finalStateReady = shadowStateReady && causticStateReady && surfelStateReady;
    return context->finalStateReady;
}


[[nodiscard]] bool FrameExecuteHistoryCopy::AcceptHistoryCopyFinalState(
    void* const rawContext,
    const Core::QueueSubmissionToken& token
){
    static_cast<void>(token);
    FrameExecuteHistoryCopy::Context* const context =
        static_cast<Context*>(rawContext)
    ;
    if(
        !context
        || !context->renderer
        || !context->shadowStateCandidate
        || !context->causticStateCandidate
        || !context->surfelStateCandidate
        || !context->finalStateReady
    )
        return false;

    const bool shadowStateReady = context->renderer->m_shadowVisibilityReturnState.commit(
        *context->shadowStateCandidate
    );
    const bool causticStateReady = context->renderer->m_causticIrradianceReturnState.commit(
        *context->causticStateCandidate
    );
    const bool surfelStateReady = context->renderer->m_surfelIrradianceReturnState.commit(
        *context->surfelStateCandidate
    );
    context->acceptedStateReady = shadowStateReady && causticStateReady && surfelStateReady;
    return context->acceptedStateReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END
