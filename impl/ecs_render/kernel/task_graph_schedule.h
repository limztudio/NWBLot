// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "frame_execution_plan.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// This is deliberately separate from FrameExecutionPlan. It declares the remaining packet-owned semantic work for
// parity telemetry; each migrated renderer task declares and submits its own graph independently.
struct GpuTaskGraphFrameScheduleInput{
    bool dedicatedAsyncCompute = false;
    bool frameLaggedAsyncLightingEnabled = false;
    bool laggedLightingHistoryReady = false;
    bool laggedLightingHistoryAccepted = false;
    bool hasTransparentRenderers = false;
    // Selects the hardware or software caustics graph producer; it is not legacy packet-plan work.
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
    {
        const auto enable = [&](const FrameExecutionWork::Enum work){
            m_workEnabled[static_cast<usize>(work)] = true;
        };
        enable(FrameExecutionWork::GraphicsPrefix);
        enable(FrameExecutionWork::AvboitRaster);
        enable(FrameExecutionWork::GraphicsPresent);
        if(m_usesAsyncAvboit){
            enable(FrameExecutionWork::AvboitDepthWarp);
            enable(FrameExecutionWork::AvboitExtinction);
            enable(FrameExecutionWork::AvboitIntegration);
            enable(FrameExecutionWork::AvboitAccumulation);
        }
    }


public:
    [[nodiscard]] bool hasWork(const FrameExecutionWork::Enum work)const noexcept{
        return work < FrameExecutionWork::kCount && m_workEnabled[static_cast<usize>(work)];
    }
    [[nodiscard]] bool usesDedicatedAsyncCompute()const noexcept{ return m_usesDedicatedAsyncCompute; }
    [[nodiscard]] bool usesLaggedLightingHistory()const noexcept{ return m_usesLaggedLightingHistory; }
    [[nodiscard]] bool capturesLaggedLightingHistory()const noexcept{ return m_capturesLaggedLightingHistory; }
    [[nodiscard]] bool usesAsyncAvboit()const noexcept{ return m_usesAsyncAvboit; }
    // These are semantic ordering constraints, not packet-copying. Resource hazards provide the remaining edges.
    [[nodiscard]] bool workDependsOn(
        const FrameExecutionWork::Enum consumer,
        const FrameExecutionWork::Enum producer
    )const noexcept{
        if(!hasWork(consumer) || !hasWork(producer))
            return false;

        switch(consumer){
        case FrameExecutionWork::AvboitRaster:
            return producer == FrameExecutionWork::GraphicsPrefix;
        case FrameExecutionWork::AvboitDepthWarp:
            return producer == FrameExecutionWork::AvboitRaster;
        case FrameExecutionWork::AvboitExtinction:
            return producer == FrameExecutionWork::AvboitDepthWarp;
        case FrameExecutionWork::AvboitIntegration:
            return producer == FrameExecutionWork::AvboitExtinction;
        case FrameExecutionWork::AvboitAccumulation:
            return producer == FrameExecutionWork::AvboitIntegration;
        case FrameExecutionWork::GraphicsPresent:
            return false;
        default:
            return false;
        }
    }


private:
    bool m_workEnabled[FrameExecutionWork::kCount] = {};
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
