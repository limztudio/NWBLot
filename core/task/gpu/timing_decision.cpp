// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "timing_decision.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuTaskTimingDecision::historyMeetsMinimumSamples(const GpuTaskTimingHistory& history, const GpuTaskTimingFeedbackPolicy& policy)noexcept{
    return policy.valid() && history.valid() && history.sampleCount >= policy.minimumSampleCount;
}


b8 GpuTaskTimingDecision::benefitExceedsHysteresis(const GpuTaskTimingHistory& incumbent, const GpuTaskTimingHistory& candidate, const GpuTaskTimingFeedbackPolicy& policy)noexcept{
    if(!policy.valid() || !incumbent.valid() || !candidate.valid() || candidate.averageSeconds >= incumbent.averageSeconds)
        return false;

    const f64 benefitSeconds = incumbent.averageSeconds - candidate.averageSeconds;
    if(benefitSeconds < policy.minimumAbsoluteBenefitSeconds)
        return false;
    return (benefitSeconds / incumbent.averageSeconds) >= policy.minimumRelativeBenefit;
}


b8 GpuTaskTimingDecision::switchDwellElapsed(const GpuTaskTimingAssignmentState& assignment, const u64 frameIndex, const GpuTaskTimingFeedbackPolicy& policy)noexcept{
    if(!policy.valid() || !assignment.valid() || frameIndex < assignment.lastSwitchFrameIndex)
        return false;
    return frameIndex - assignment.lastSwitchFrameIndex >= policy.minimumFramesBetweenSwitches;
}


b8 GpuTaskTimingDecision::canSwitch(
    const GpuTaskTimingHistory& incumbent,
    const GpuTaskTimingHistory& candidate,
    const GpuTaskTimingAssignmentState& assignment,
    const GpuPhysicalQueueId& incumbentQueue,
    const GpuPhysicalQueueId& candidateQueue,
    const u64 frameIndex,
    const GpuTaskTimingFeedbackPolicy& policy
)noexcept{
    return
        policy.enabled
        && incumbentQueue.valid()
        && candidateQueue.valid()
        && incumbentQueue != candidateQueue
        && incumbentQueue.deviceGeneration == candidateQueue.deviceGeneration
        && assignment.valid()
        && assignment.lastAcceptedQueue == incumbentQueue
        && historyMeetsMinimumSamples(incumbent, policy)
        && historyMeetsMinimumSamples(candidate, policy)
        && benefitExceedsHysteresis(incumbent, candidate, policy)
        && switchDwellElapsed(assignment, frameIndex, policy)
    ;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


