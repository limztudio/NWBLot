// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "presentation_pacing_ring.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void PresentationPacingRing::reset()noexcept{
    m_count = 0u;
    m_head = 0u;
    m_previousCount = 0u;
    m_previousObservationTime = {};
    m_previousPresentationTime = {};
    m_started = false;
    m_pendingStallCounted = false;
    m_stalls = 0u;
}

void PresentationPacingRing::record(const u64 successfulPresentations, const Timer now)noexcept{
    if(!m_started){
        m_started = true;
        m_previousCount = successfulPresentations;
        m_previousObservationTime = now;
        m_previousPresentationTime = now;
        return;
    }
    if(now < m_previousObservationTime || successfulPresentations < m_previousCount){
        reset();
        return;
    }
    m_previousObservationTime = now;
    const f64 wallIntervalMs = DurationInSeconds<f64>(now, m_previousPresentationTime) * s_MsPerSecond;
    const u64 advanced = successfulPresentations - m_previousCount;
    if(advanced == 0u){
        if(!m_pendingStallCounted && wallIntervalMs >= s_StallThresholdMs){
            ++m_stalls;
            m_pendingStallCounted = true;
        }
        return;
    }
    const f64 wallMs = wallIntervalMs / static_cast<f64>(advanced);
    if(!m_pendingStallCounted && wallMs >= s_StallThresholdMs)
        ++m_stalls;
    m_samples[m_head] = PresentationPacingSample{ wallMs };
    m_head = (m_head + 1u) % s_Capacity;
    if(m_count < s_Capacity)
        ++m_count;
    m_previousCount = successfulPresentations;
    m_previousPresentationTime = now;
    m_pendingStallCounted = false;
}

PresentationPacingSummary PresentationPacingRing::summarize()const{
    PresentationPacingSummary summary{};
    summary.stallsOver50Ms = m_stalls;
    if(m_count == 0u)
        return summary;
    Array<f64, s_Capacity> sorted{};
    for(usize i = 0u; i < m_count; ++i)
        sorted[i] = m_samples[(m_head + s_Capacity - m_count + i) % s_Capacity].wallMsPerPresentation;
    Sort(sorted.begin(), sorted.begin() + m_count);
    summary.samples = m_count;
    summary.p50Ms = sorted[(m_count * 50u) / 100u >= m_count ? m_count - 1u : (m_count * 50u) / 100u];
    summary.p95Ms = sorted[(m_count * 95u) / 100u >= m_count ? m_count - 1u : (m_count * 95u) / 100u];
    summary.maxMs = sorted[m_count - 1u];
    return summary;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

