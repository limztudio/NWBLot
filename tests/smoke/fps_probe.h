// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/common/log.h>
#include <global/math/vector_double.h>
#include <global/simplemath.h>
#include <global/type.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FpsProbe final{
public:
    explicit FpsProbe(TStringView label)
        : m_label(label)
    {}

    void recordFrame(const f32 delta){
        const f64 safeDelta = IsFinite(delta) && delta > 0.0f ? static_cast<f64>(delta) : 0.0;
        if(safeDelta <= 0.0)
            return;

        m_elapsedSeconds += safeDelta;
        if(m_elapsedSeconds < s_WarmupSeconds)
            return;

        if(!m_samplingStarted){
            m_samplingStarted = true;
            m_intervalSeconds = 0.0;
            m_intervalFrames = 0u;
            m_minFrameSeconds = 0.0;
            m_maxFrameSeconds = 0.0;
        }

        m_intervalSeconds += safeDelta;
        ++m_intervalFrames;
        m_minFrameSeconds = m_intervalFrames == 1u ? safeDelta : Min(m_minFrameSeconds, safeDelta);
        m_maxFrameSeconds = Max(m_maxFrameSeconds, safeDelta);

        if(m_intervalSeconds < s_ReportIntervalSeconds || m_intervalFrames == 0u)
            return;

        const f64 averageFrameSeconds = m_intervalSeconds / static_cast<f64>(m_intervalFrames);
        const f64 averageFps = 1.0 / averageFrameSeconds;
        const SIMDVectorDouble rangeMs = SIMDVectorDouble{ m_minFrameSeconds, m_maxFrameSeconds } * s_MillisecondsPerSecond;
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("{}: fps avg={} frame_ms avg={} min={} max={} frames={} seconds={}")
            , m_label
            , averageFps
            , averageFrameSeconds * s_MillisecondsPerSecond
            , rangeMs.x
            , rangeMs.y
            , m_intervalFrames
            , m_intervalSeconds
        );

        m_intervalSeconds = 0.0;
        m_intervalFrames = 0u;
        m_minFrameSeconds = 0.0;
        m_maxFrameSeconds = 0.0;
    }


private:
    static constexpr f64 s_WarmupSeconds = 0.25;
    static constexpr f64 s_ReportIntervalSeconds = 0.5;
    static constexpr f64 s_MillisecondsPerSecond = 1000.0;

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

