// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "presentation_fps_probe.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


PresentationFpsProbe::PresentationFpsProbe(const f64 warmupSeconds, const f64 measurementSeconds)
    : m_warmupSeconds(warmupSeconds)
    , m_measurementSeconds(measurementSeconds)
    , m_invalid(!IsFinite(warmupSeconds) || warmupSeconds < 0.0 || !IsFinite(measurementSeconds) || measurementSeconds < 0.0)
{}

PresentationFpsStatus::Enum PresentationFpsProbe::observe(const u64 successfulPresentations, const Timer now){
    if(m_invalid)
        return PresentationFpsStatus::Invalid;
    if(m_complete)
        return PresentationFpsStatus::Waiting;
    if(!m_started){
        m_started = true;
        m_warmupBegin = now;
        m_previousObservation = now;
        m_previousPresentationCount = successfulPresentations;
    }
    if(now < m_previousObservation || successfulPresentations < m_previousPresentationCount){
        m_invalid = true;
        return PresentationFpsStatus::Invalid;
    }
    m_previousObservation = now;
    m_previousPresentationCount = successfulPresentations;

    if(!m_measuring){
        if(DurationInSeconds<f64>(now, m_warmupBegin) < m_warmupSeconds)
            return PresentationFpsStatus::Waiting;
        m_measuring = true;
        m_measurementBegin = now;
        m_intervalBegin = now;
        m_intervalPresentationCount = successfulPresentations;
        m_total.firstPresentationCount = successfulPresentations;
        m_total.lastPresentationCount = successfulPresentations;
        return PresentationFpsStatus::Waiting;
    }

    m_total.lastPresentationCount = successfulPresentations;
    m_total.wallSeconds = DurationInSeconds<f64>(now, m_measurementBegin);
    const f64 intervalSeconds = DurationInSeconds<f64>(now, m_intervalBegin);
    m_complete = m_measurementSeconds > 0.0 && m_total.wallSeconds >= m_measurementSeconds;
    if(!m_complete && intervalSeconds < 0.5)
        return PresentationFpsStatus::Waiting;

    m_interval = {
        .firstPresentationCount = m_intervalPresentationCount,
        .lastPresentationCount = successfulPresentations,
        .wallSeconds = intervalSeconds,
    };
    m_intervalBegin = now;
    m_intervalPresentationCount = successfulPresentations;
    return m_complete ? PresentationFpsStatus::Complete : PresentationFpsStatus::Interval;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

