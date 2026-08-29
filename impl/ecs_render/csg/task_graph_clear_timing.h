// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/kernel/timing_names.h>

#include <core/graphics/gpu_timing.h>
#include <core/graphics/module.h>
#include <core/graphics/task_graph/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Opaque prefix timing tickets are rebound after compilation, whereas transparent CSG keeps AVBOIT Pre's
// stable ticket. The rectangular clear pair resolves either form while preserving one timing range.
struct CsgIntervalClearTimingRecordState{
    Core::Graphics* graphics = nullptr;
    Optional<Core::GpuTimingMeasure>* timing = nullptr;
    Core::GpuTimingSubmissionTicket** rebindableTimingTicket = nullptr;
    Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
};


[[nodiscard]] inline Core::GpuTimingSubmissionTicket* ResolveCsgIntervalClearTimingTicket(
    CsgIntervalClearTimingRecordState& state
){
    return state.rebindableTimingTicket ? *state.rebindableTimingTicket : state.timingTicket;
}

// The CSG work-region clear now records as two typed rectangle primitives. These hooks retain its former one-range
// measurement even though the compiler owns each individual CopyDest operation and their UAV handoffs.
[[nodiscard]] inline bool BeginCsgIntervalClearTiming(
    void* const rawState,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    CsgIntervalClearTimingRecordState* const state = static_cast<CsgIntervalClearTimingRecordState*>(rawState);
    if(!state || !state->graphics || !state->timing || *state->timing)
        return false;
    Core::GpuTimingSubmissionTicket* const timingTicket = ResolveCsgIntervalClearTimingTicket(*state);
    if(!timingTicket)
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*timingTicket);
    state->timing->emplace(
        state->graphics->gpuTiming(),
        RendererGpuTimingScope::s_CsgIntervalClear,
        state->graphics->getDevice(),
        commandList
    );
    state->timing->value().finishMarker();
    return true;
}

[[nodiscard]] inline bool EndCsgIntervalClearTiming(
    void* const rawState,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    static_cast<void>(context);
    CsgIntervalClearTimingRecordState* const state = static_cast<CsgIntervalClearTimingRecordState*>(rawState);
    if(!state || !state->timing || !*state->timing)
        return false;
    Core::GpuTimingSubmissionTicket* const timingTicket = ResolveCsgIntervalClearTimingTicket(*state);
    if(!timingTicket)
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*timingTicket);
    state->timing->value().finishTiming(commandList);
    state->timing->reset();
    return true;
}

inline void DiscardCsgIntervalClearTiming(void* const rawState){
    CsgIntervalClearTimingRecordState* const state = static_cast<CsgIntervalClearTimingRecordState*>(rawState);
    if(!state || !state->timing || !*state->timing)
        return;
    state->timing->value().discardTiming();
    state->timing->reset();
}


// The opaque material draw ordering and CSG CPU frame data are captured while the graph is declared.  The paired
// instance/material blobs are therefore immutable packet inputs rather than data rebuilt while a native task records.


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

