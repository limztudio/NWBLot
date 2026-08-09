// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// This selects graph-owned renderer routes and history availability. Packet submission is graph-native; imported
// command-list bundles remain a temporary recording bridge.
struct GpuTaskGraphFrameScheduleInput{
    bool dedicatedAsyncCompute = false;
    bool frameLaggedAsyncLightingEnabled = false;
    bool laggedLightingHistoryReady = false;
    bool laggedLightingHistoryAccepted = false;
    bool hasTransparentRenderers = false;
    // Selects the hardware or software caustics graph producer.
    bool hardwareCaustics = false;
};


class GpuTaskGraphFrameSchedule final{
public:
    explicit GpuTaskGraphFrameSchedule(const GpuTaskGraphFrameScheduleInput& input)noexcept
        : m_usesDedicatedAsyncCompute(input.dedicatedAsyncCompute)
        , m_usesLaggedLightingHistory(
            input.dedicatedAsyncCompute
            && input.frameLaggedAsyncLightingEnabled
            && input.laggedLightingHistoryReady
            && input.laggedLightingHistoryAccepted
        )
        , m_capturesLaggedLightingHistory(
            input.dedicatedAsyncCompute
            && input.frameLaggedAsyncLightingEnabled
        )
        , m_usesAsyncAvboit(
            input.dedicatedAsyncCompute
            && input.hasTransparentRenderers
            && !m_usesLaggedLightingHistory
        )
    {}


public:
    [[nodiscard]] bool usesDedicatedAsyncCompute()const noexcept{ return m_usesDedicatedAsyncCompute; }
    [[nodiscard]] bool usesLaggedLightingHistory()const noexcept{ return m_usesLaggedLightingHistory; }
    [[nodiscard]] bool capturesLaggedLightingHistory()const noexcept{ return m_capturesLaggedLightingHistory; }
    [[nodiscard]] bool usesAsyncAvboit()const noexcept{ return m_usesAsyncAvboit; }
private:
    bool m_usesDedicatedAsyncCompute = false;
    bool m_usesLaggedLightingHistory = false;
    bool m_capturesLaggedLightingHistory = false;
    bool m_usesAsyncAvboit = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
