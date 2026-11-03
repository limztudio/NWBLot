// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "fps_probe.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void FpsProbe::recordFrame(const f32 delta){
    const f64 safeDelta = IsFinite(delta) ? Max(static_cast<f64>(delta), 0.0) : 0.0;
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
    const SIMDVectorDouble frameSeconds = VectorDoubleReplicate(safeDelta);
    const SIMDVectorDouble extremaSeconds = VectorDoubleSet(m_minFrameSeconds, m_maxFrameSeconds, 0.0, 0.0);
    const SIMDVectorDouble nextExtremaSeconds = VectorDoubleSelect(VectorDoubleMin(extremaSeconds, frameSeconds), VectorDoubleMax(extremaSeconds, frameSeconds), s_DoubleSIMDMaskY);
    m_minFrameSeconds = m_intervalFrames == 1u ? safeDelta : VectorDoubleGetX(nextExtremaSeconds);
    m_maxFrameSeconds = VectorDoubleGetY(nextExtremaSeconds);

    if(m_intervalSeconds < s_ReportIntervalSeconds || m_intervalFrames == 0u)
        return;

    const f64 averageFrameSeconds = m_intervalSeconds / static_cast<f64>(m_intervalFrames);
    const f64 averageFps = 1.0 / averageFrameSeconds;
    // Lanes are average, min and max frame time.
    const SIMDVectorDouble frameMs = VectorDoubleScale(VectorDoubleSet(averageFrameSeconds, m_minFrameSeconds, m_maxFrameSeconds, 0.0), s_MillisecondsPerSecond);
    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("{}: fps avg={} frame_ms avg={} min={} max={} frames={} seconds={}")
        , m_label
        , averageFps
        , VectorDoubleGetX(frameMs)
        , VectorDoubleGetY(frameMs)
        , VectorDoubleGetZ(frameMs)
        , m_intervalFrames
        , m_intervalSeconds
    );

    m_intervalSeconds = 0.0;
    m_intervalFrames = 0u;
    m_minFrameSeconds = 0.0;
    m_maxFrameSeconds = 0.0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

