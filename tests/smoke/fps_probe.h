// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/common/log.h>
#include <global/simplemath.h>
#include <global/type.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FpsProbe final{
public:
    explicit FpsProbe(const tchar* label)
        : m_label(label)
    {}

    void recordFrame(const f32 delta){
        const f64 safeDelta = IsFinite(delta) && delta > 0.0f ? static_cast<f64>(delta) : 0.0;
        if(safeDelta <= 0.0)
            return;
        if(safeDelta > s_MaxMeasuredFrameSeconds)
            return;

        m_elapsedSeconds += safeDelta;
        if(m_elapsedSeconds < s_WarmupSeconds)
            return;

        if(!m_samplingStarted){
            m_samplingStarted = true;
            m_intervalSeconds = 0.0;
            m_intervalFrames = 0u;
            m_minFrameSeconds = s_LargeFrameSeconds;
            m_maxFrameSeconds = 0.0;
        }

        m_intervalSeconds += safeDelta;
        ++m_intervalFrames;
        m_minFrameSeconds = Min(m_minFrameSeconds, safeDelta);
        m_maxFrameSeconds = Max(m_maxFrameSeconds, safeDelta);

        if(m_intervalSeconds < s_ReportIntervalSeconds || m_intervalFrames == 0u)
            return;

        const f64 averageFrameSeconds = m_intervalSeconds / static_cast<f64>(m_intervalFrames);
        const f64 averageFps = 1.0 / averageFrameSeconds;
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("{}: fps avg={} frame_ms avg={} min={} max={} frames={} seconds={}")
            , m_label
            , averageFps
            , averageFrameSeconds * 1000.0
            , m_minFrameSeconds * 1000.0
            , m_maxFrameSeconds * 1000.0
            , m_intervalFrames
            , m_intervalSeconds
        );

        m_intervalSeconds = 0.0;
        m_intervalFrames = 0u;
        m_minFrameSeconds = s_LargeFrameSeconds;
        m_maxFrameSeconds = 0.0;
    }


private:
    static constexpr f64 s_WarmupSeconds = 0.25;
    static constexpr f64 s_ReportIntervalSeconds = 0.5;
    static constexpr f64 s_MaxMeasuredFrameSeconds = 0.25;
    static constexpr f64 s_LargeFrameSeconds = 3600.0;

    const tchar* m_label = NWB_TEXT("Smoke");
    f64 m_elapsedSeconds = 0.0;
    f64 m_intervalSeconds = 0.0;
    f64 m_minFrameSeconds = s_LargeFrameSeconds;
    f64 m_maxFrameSeconds = 0.0;
    u32 m_intervalFrames = 0u;
    bool m_samplingStarted = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
