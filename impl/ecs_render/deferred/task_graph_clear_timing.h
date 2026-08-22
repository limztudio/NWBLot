// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/kernel/renderer_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BeginDeferredClearTiming(
    void* const rawState,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    DeferredClearTimingRecordState* const state = static_cast<DeferredClearTimingRecordState*>(rawState);
    if(
        !state
        || !state->graphics
        || !state->timing
        || !state->timingTicket
        || !*state->timingTicket
        || *state->timing
    )
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(**state->timingTicket);
    state->timing->emplace(
        state->graphics->gpuTiming(),
        RendererGpuTimingScope::s_DeferredClear,
        state->graphics->getDevice(),
        commandList
    );
    state->timing->value().finishMarker();
    return true;
}

[[nodiscard]] inline bool EndDeferredClearTiming(
    void* const rawState,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    DeferredClearTimingRecordState* const state = static_cast<DeferredClearTimingRecordState*>(rawState);
    if(
        !state
        || !state->timing
        || !*state->timing
        || !state->timingTicket
        || !*state->timingTicket
    )
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(**state->timingTicket);
    state->timing->value().finishTiming(commandList);
    state->timing->reset();
    return true;
}

inline void DiscardDeferredClearTiming(void* const rawState){
    DeferredClearTimingRecordState* const state = static_cast<DeferredClearTimingRecordState*>(rawState);
    if(!state || !state->timing || !*state->timing)
        return;
    state->timing->value().discardTiming();
    state->timing->reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

