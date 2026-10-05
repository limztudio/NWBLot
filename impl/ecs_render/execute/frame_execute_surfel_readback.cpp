// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/execute/frame_execute_surfel_readback.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool FrameExecuteSurfelReadback::PrepareReadbackFinalState(
    void* const rawContext,
    const Core::CommandListResourceStateHandoff* const finalState
){
    FrameExecuteSurfelReadback::Context* const context =
        static_cast<Context*>(rawContext)
    ;
    if(!context || !context->renderer || !context->candidate || !context->buffers)
        return false;
    context->finalStateReady = finalState
        && context->renderer->m_surfelGiCounterPersistentState.buildFilteredBufferSubset(
            *context->candidate,
            *finalState,
            context->buffers,
            context->bufferCount,
            context->scratchArena
        )
    ;
    return context->finalStateReady;
}


[[nodiscard]] bool FrameExecuteSurfelReadback::AcceptReadbackFinalState(
    void* const rawContext,
    const Core::QueueSubmissionToken& token
){
    static_cast<void>(token);
    FrameExecuteSurfelReadback::Context* const context =
        static_cast<Context*>(rawContext)
    ;
    if(!context || !context->renderer || !context->candidate || !context->finalStateReady)
        return false;
    context->acceptedStateReady = context->renderer->m_surfelGiCounterPersistentState.commit(
        *context->candidate
    );
    if(context->acceptedStateReady)
        context->renderer->m_raytracingSystem.confirmSurfelCountReadbackSubmission(token);
    return context->acceptedStateReady;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END
