// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "timing_feedback.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTaskTimingDecision final{
public:
    [[nodiscard]] static b8 HistoryMeetsMinimumSamples(const GpuTaskTimingHistory& history, const GpuTaskTimingFeedbackPolicy& policy)noexcept;
    [[nodiscard]] static b8 BenefitExceedsHysteresis(const GpuTaskTimingHistory& incumbent, const GpuTaskTimingHistory& candidate, const GpuTaskTimingFeedbackPolicy& policy)noexcept;
    [[nodiscard]] static b8 SwitchDwellElapsed(const GpuTaskTimingAssignmentState& assignment, u64 frameIndex, const GpuTaskTimingFeedbackPolicy& policy)noexcept;
    [[nodiscard]] static b8 CanSwitch(
        const GpuTaskTimingHistory& incumbent,
        const GpuTaskTimingHistory& candidate,
        const GpuTaskTimingAssignmentState& assignment,
        const GpuPhysicalQueueId& incumbentQueue,
        const GpuPhysicalQueueId& candidateQueue,
        u64 frameIndex,
        const GpuTaskTimingFeedbackPolicy& policy
    )noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


