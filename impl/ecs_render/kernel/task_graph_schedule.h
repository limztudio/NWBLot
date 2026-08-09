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
        , m_hardwareCaustics(input.hardwareCaustics)
    {
        const auto enable = [&](const FrameExecutionWork::Enum work){
            m_workEnabled[static_cast<usize>(work)] = true;
        };
        enable(FrameExecutionWork::GraphicsPrefix);
        enable(FrameExecutionWork::RayEffects);
        if(m_hardwareCaustics)
            enable(FrameExecutionWork::HardwareCaustics);
        enable(FrameExecutionWork::AvboitRaster);
        enable(FrameExecutionWork::GraphicsPresent);
        if(m_usesDedicatedAsyncCompute)
            enable(FrameExecutionWork::AsyncEffectsTiming);
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
    [[nodiscard]] bool workWaitsForLaggedLightingHistory(
        const FrameExecutionWork::Enum work
    )const noexcept{
        if(!m_usesLaggedLightingHistory || !hasWork(work))
            return false;
        return work == FrameExecutionWork::HardwareCaustics && m_hardwareCaustics;
    }
    // These are semantic ordering constraints, not packet-copying. Resource hazards provide the remaining edges.
    [[nodiscard]] bool workDependsOn(
        const FrameExecutionWork::Enum consumer,
        const FrameExecutionWork::Enum producer
    )const noexcept{
        if(!hasWork(consumer) || !hasWork(producer))
            return false;

        switch(consumer){
        case FrameExecutionWork::RayEffects:
        case FrameExecutionWork::HardwareCaustics:
        case FrameExecutionWork::AvboitRaster:
        case FrameExecutionWork::AsyncEffectsTiming:
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
            return m_usesLaggedLightingHistory && producer == FrameExecutionWork::RayEffects;
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
    bool m_hardwareCaustics = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
