// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "timing_decision.h"

#include <global/simd/vector_arithmetic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuTaskTimingDecision::HistoryMeetsMinimumSamples(const GpuTaskTimingHistory& history, const GpuTaskTimingFeedbackPolicy& policy)noexcept{
    return policy.valid() && history.valid() && history.sampleCount >= policy.minimumSampleCount;
}


b8 GpuTaskTimingDecision::BenefitExceedsHysteresis(const GpuTaskTimingHistory& incumbent, const GpuTaskTimingHistory& candidate, const GpuTaskTimingFeedbackPolicy& policy)noexcept{
    if(!policy.valid() || !incumbent.valid() || !candidate.valid() || candidate.averageSeconds >= incumbent.averageSeconds)
        return false;

    const f64 benefitSeconds = VectorDoubleGetX(VectorDoubleSubtract(VectorDoubleReplicate(incumbent.averageSeconds), VectorDoubleReplicate(candidate.averageSeconds)));
    if(benefitSeconds < policy.minimumAbsoluteBenefitSeconds)
        return false;
    return VectorDoubleGetX(VectorDoubleDivide(VectorDoubleReplicate(benefitSeconds), VectorDoubleReplicate(incumbent.averageSeconds))) >= policy.minimumRelativeBenefit;
}


b8 GpuTaskTimingDecision::SwitchDwellElapsed(const GpuTaskTimingAssignmentState& assignment, const u64 frameIndex, const GpuTaskTimingFeedbackPolicy& policy)noexcept{
    if(!policy.valid() || !assignment.valid() || frameIndex < assignment.lastSwitchFrameIndex)
        return false;
    return frameIndex - assignment.lastSwitchFrameIndex >= policy.minimumFramesBetweenSwitches;
}


b8 GpuTaskTimingDecision::CanSwitch(
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
        && GpuTaskTimingDecision::HistoryMeetsMinimumSamples(incumbent, policy)
        && GpuTaskTimingDecision::HistoryMeetsMinimumSamples(candidate, policy)
        && GpuTaskTimingDecision::BenefitExceedsHysteresis(incumbent, candidate, policy)
        && GpuTaskTimingDecision::SwitchDwellElapsed(assignment, frameIndex, policy)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

