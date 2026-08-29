// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "task_graph_clear_timing_state.h"

#include <impl/ecs_render/kernel/timing_names.h>

#include <core/graphics/module.h>
#include <core/graphics/task_graph/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BeginAvboitClearTiming(
    void* const rawState,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    AvboitClearTimingRecordState* const state = static_cast<AvboitClearTimingRecordState*>(rawState);
    if(
        !state
        || !state->graphics
        || !state->timing
        || !state->timingTicket
        || *state->timing
    )
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*state->timingTicket);
    state->timing->emplace(
        state->graphics->gpuTiming(),
        RendererGpuTimingScope::s_AvboitClear,
        state->graphics->getDevice(),
        commandList
    );
    state->timing->value().finishMarker();
    return true;
}

[[nodiscard]] inline bool EndAvboitClearTiming(
    void* const rawState,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    AvboitClearTimingRecordState* const state = static_cast<AvboitClearTimingRecordState*>(rawState);
    if(
        !state
        || !state->timing
        || !*state->timing
        || !state->timingTicket
    )
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*state->timingTicket);
    state->timing->value().finishTiming(commandList);
    state->timing->reset();
    return true;
}

inline void DiscardAvboitClearTiming(void* const rawState){
    AvboitClearTimingRecordState* const state = static_cast<AvboitClearTimingRecordState*>(rawState);
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

