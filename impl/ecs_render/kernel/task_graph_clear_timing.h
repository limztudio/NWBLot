// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "timing_names.h"

#include <core/graphics/module.h>
#include <core/graphics/task_graph/task_desc.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GraphClearTimingRecordState{
    Core::Graphics* graphics = nullptr;
    Optional<Core::GpuTimingMeasure>* timing = nullptr;
    Core::GpuTimingSubmissionTicket** rebindableTimingTicket = nullptr;
    Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
    Core::GpuTimingScopeDefinition scope;
};


[[nodiscard]] inline Core::GpuTimingSubmissionTicket* ResolveGraphClearTimingTicket(
    const GraphClearTimingRecordState& state
){
    return state.rebindableTimingTicket ? *state.rebindableTimingTicket : state.timingTicket;
}

[[nodiscard]] inline bool BeginGraphClearTiming(
    Core::Graphics* const graphics,
    Optional<Core::GpuTimingMeasure>* const timing,
    Core::GpuTimingSubmissionTicket* const timingTicket,
    const Core::GpuTimingScopeDefinition& scope,
    Core::CommandList& commandList
){
    if(!graphics || !timing || !timingTicket || *timing)
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*timingTicket);
    timing->emplace(
        graphics->gpuTiming(),
        scope,
        graphics->getDevice(),
        commandList
    );
    timing->value().finishMarker();
    return true;
}

[[nodiscard]] inline bool EndGraphClearTiming(
    Optional<Core::GpuTimingMeasure>* const timing,
    Core::GpuTimingSubmissionTicket* const timingTicket,
    Core::CommandList& commandList
){
    if(!timing || !*timing || !timingTicket)
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*timingTicket);
    timing->value().finishTiming(commandList);
    timing->reset();
    return true;
}

[[nodiscard]] inline bool BeginGraphClearTimingRecord(
    void* const rawState,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    GraphClearTimingRecordState* const state = static_cast<GraphClearTimingRecordState*>(rawState);
    if(!state)
        return false;
    return BeginGraphClearTiming(
        state->graphics,
        state->timing,
        ResolveGraphClearTimingTicket(*state),
        state->scope,
        commandList
    );
}

[[nodiscard]] inline bool EndGraphClearTimingRecord(
    void* const rawState,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    GraphClearTimingRecordState* const state = static_cast<GraphClearTimingRecordState*>(rawState);
    if(!state)
        return false;
    return EndGraphClearTiming(state->timing, ResolveGraphClearTimingTicket(*state), commandList);
}

inline void DiscardGraphClearTimingRecord(void* const rawState){
    GraphClearTimingRecordState* const state = static_cast<GraphClearTimingRecordState*>(rawState);
    if(!state)
        return;
    DiscardGpuTimingMeasure(state->timing);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

