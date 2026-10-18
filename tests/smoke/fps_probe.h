// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/common/log.h>

#include <global/simd/vector.h>
#include <global/simplemath.h>
#include <global/type.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FpsProbe final{
private:
    static constexpr f64 s_WarmupSeconds = 0.25;
    static constexpr f64 s_ReportIntervalSeconds = 0.5;
    static constexpr f64 s_MillisecondsPerSecond = 1000.0;


public:
    explicit FpsProbe(TStringView label)noexcept
        : m_label(label)
    {}


public:
    void recordFrame(const f32 delta){
        const f64 safeDelta = IsFinite(delta) ? VectorDoubleGetX(VectorDoubleMax(VectorDoubleReplicate(static_cast<f64>(delta)), VectorDoubleZero())) : 0.0;
        if(safeDelta <= 0.0)
            return;

        const SIMDVectorDouble frameSeconds = VectorDoubleReplicate(safeDelta);
        m_elapsedSeconds = VectorDoubleGetX(VectorDoubleAdd(VectorDoubleReplicate(m_elapsedSeconds), frameSeconds));
        if(m_elapsedSeconds < s_WarmupSeconds)
            return;

        if(!m_samplingStarted){
            m_samplingStarted = true;
            m_intervalSeconds = 0.0;
            m_intervalFrames = 0u;
            m_minFrameSeconds = 0.0;
            m_maxFrameSeconds = 0.0;
        }

        m_intervalSeconds = VectorDoubleGetX(VectorDoubleAdd(VectorDoubleReplicate(m_intervalSeconds), frameSeconds));
        ++m_intervalFrames;
        const SIMDVectorDouble extremaSeconds = VectorDoubleSet(m_minFrameSeconds, m_maxFrameSeconds, 0.0, 0.0);
        const SIMDVectorDouble nextExtremaSeconds = VectorDoubleSelect(VectorDoubleMin(extremaSeconds, frameSeconds), VectorDoubleMax(extremaSeconds, frameSeconds), s_DoubleSIMDMaskY);
        m_minFrameSeconds = m_intervalFrames == 1u ? safeDelta : VectorDoubleGetX(nextExtremaSeconds);
        m_maxFrameSeconds = VectorDoubleGetY(nextExtremaSeconds);

        if(m_intervalSeconds < s_ReportIntervalSeconds || m_intervalFrames == 0u)
            return;

        const SIMDVectorDouble averageFrameSeconds = VectorDoubleDivide(VectorDoubleReplicate(m_intervalSeconds), VectorDoubleReplicate(static_cast<f64>(m_intervalFrames)));
        const f64 averageFps = VectorDoubleGetX(VectorDoubleDivide(VectorDoubleSplatOne(), averageFrameSeconds));
        // Lanes are average, min and max frame time.
        const SIMDVectorDouble frameMs = VectorDoubleScale(VectorDoubleSelect(VectorDoubleSet(0.0, m_minFrameSeconds, m_maxFrameSeconds, 0.0), averageFrameSeconds, s_DoubleSIMDMaskX), s_MillisecondsPerSecond);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("{}: fps avg={} frame_ms avg={} min={} max={} frames={} seconds={}"), m_label, averageFps, VectorDoubleGetX(frameMs), VectorDoubleGetY(frameMs), VectorDoubleGetZ(frameMs), m_intervalFrames, m_intervalSeconds);

        m_intervalSeconds = 0.0;
        m_intervalFrames = 0u;
        m_minFrameSeconds = 0.0;
        m_maxFrameSeconds = 0.0;
    }


private:
    TStringView m_label = NWB_TEXT("Smoke");
    f64 m_elapsedSeconds = 0.0;
    f64 m_intervalSeconds = 0.0;
    f64 m_minFrameSeconds = 0.0;
    f64 m_maxFrameSeconds = 0.0;
    u32 m_intervalFrames = 0u;
    bool m_samplingStarted = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

