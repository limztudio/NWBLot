// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/global.h>
#include <impl/ecs_render/kernel/task_timing_feedback.h>

#include <core/graphics/gpu_timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererAvboitSystem;
class RendererTaskTimingFeedback;
struct AvboitFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AvboitComputeEffectRecordInputs{
    RendererAvboitSystem& avboitSystem;
    AvboitFrameTargets& targets;
    Core::GpuTimingSubmissionTicket& timingTicket;
    RendererTaskTimingFeedback* timingFeedback = nullptr;
    const Core::GpuTimingScopeDefinition* timingScope = nullptr;
    mutable Core::GpuTimingSampleAttribution timingAttribution = Core::s_NoGpuTimingSampleAttribution;
};

template<typename DispatchFn>
[[nodiscard]] inline b8 RecordAvboitComputeEffect(const AvboitComputeEffectRecordInputs& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context, DispatchFn&& dispatch){
    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    b8 timingRecorded = false;
    const auto timingAttribution = ECSRenderDetail::BeginTaskTimingSample(payload.timingFeedback, payload.timingScope, context);
    if(!timingAttribution){
        payload.timingAttribution = Core::s_NoGpuTimingSampleAttribution;
        return false;
    }
    payload.timingAttribution = *timingAttribution;
    dispatch(commandList, payload.targets, payload.timingAttribution, &timingRecorded);
    if(!timingRecorded && payload.timingFeedback){
        payload.timingFeedback->discardRecording(payload.timingAttribution);
        payload.timingAttribution = Core::s_NoGpuTimingSampleAttribution;
    }
    return true;
}

inline void AcceptAvboitComputeEffect(AvboitComputeEffectRecordInputs& payload, const Core::QueueSubmissionToken& token)noexcept{
    if(payload.timingFeedback)
        payload.timingFeedback->acceptSubmission(payload.timingAttribution, token);
    payload.timingAttribution = Core::s_NoGpuTimingSampleAttribution;
}

inline void DiscardAvboitComputeEffect(AvboitComputeEffectRecordInputs& payload)noexcept{
    if(payload.timingFeedback)
        payload.timingFeedback->discardRecording(payload.timingAttribution);
    payload.timingAttribution = Core::s_NoGpuTimingSampleAttribution;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

