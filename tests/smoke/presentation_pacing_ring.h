// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/common/global.h>

#include <global/algorithm.h>
#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Low-overhead per-frame pacing trace for the stress timing mode. Timing runs keep expensive counters off; this ring
// stores one compact sample per observation that advances the accepted-presentation count, so p50/p95/max describe
// actual wall pacing instead of a handful of half-second FPS summaries. Diagnostic builds may enlarge the capacity;
// timing claims always report the stored sample count alongside the percentiles.
struct PresentationPacingSample{
    f64 wallMsPerPresentation = 0.0;
    u64 newPresentations = 0u;
};

struct PresentationPacingSummary{
    usize samples = 0u;
    f64 p50Ms = 0.0;
    f64 p95Ms = 0.0;
    f64 maxMs = 0.0;
    u64 stallsOver50Ms = 0u;
};

class PresentationPacingRing final{
public:
    static constexpr usize s_Capacity = 4096u;
    static constexpr f64 s_StallThresholdMs = 50.0;
    static constexpr f64 s_MsPerSecond = 1000.0;


public:
    void reset()noexcept{
        m_count = 0u;
        m_head = 0u;
        m_previousCount = 0u;
        m_previousTime = {};
        m_started = false;
        m_stalls = 0u;
    }

    void record(u64 successfulPresentations, Timer now){
        if(!m_started){
            m_started = true;
            m_previousCount = successfulPresentations;
            m_previousTime = now;
            return;
        }
        if(now < m_previousTime || successfulPresentations < m_previousCount){
            m_started = false;
            m_count = 0u;
            m_head = 0u;
            m_stalls = 0u;
            return;
        }
        const f64 wallSeconds = DurationInSeconds<f64>(now, m_previousTime);
        const u64 advanced = successfulPresentations - m_previousCount;
        m_previousCount = successfulPresentations;
        m_previousTime = now;
        if(advanced == 0u){
            if(wallSeconds * s_MsPerSecond >= s_StallThresholdMs)
                ++m_stalls;
            return;
        }
        const f64 wallMs = wallSeconds * s_MsPerSecond / static_cast<f64>(advanced);
        if(wallMs >= s_StallThresholdMs)
            ++m_stalls;
        m_samples[m_head] = PresentationPacingSample{ wallMs, advanced };
        m_head = (m_head + 1u) % s_Capacity;
        if(m_count < s_Capacity)
            ++m_count;
    }

    [[nodiscard]] PresentationPacingSummary summarize()const{
        PresentationPacingSummary summary{};
        summary.stallsOver50Ms = m_stalls;
        if(m_count == 0u)
            return summary;
        f64 sorted[s_Capacity];
        for(usize i = 0u; i < m_count; ++i)
            sorted[i] = m_samples[(m_head + s_Capacity - m_count + i) % s_Capacity].wallMsPerPresentation;
        Sort(sorted, sorted + m_count);
        summary.samples = m_count;
        summary.p50Ms = sorted[(m_count * 50u) / 100u >= m_count ? m_count - 1u : (m_count * 50u) / 100u];
        summary.p95Ms = sorted[(m_count * 95u) / 100u >= m_count ? m_count - 1u : (m_count * 95u) / 100u];
        summary.maxMs = sorted[m_count - 1u];
        return summary;
    }

    [[nodiscard]] usize sampleCount()const noexcept{ return m_count; }
    [[nodiscard]] u64 stallCount()const noexcept{ return m_stalls; }


private:
    PresentationPacingSample m_samples[s_Capacity] = {};
    usize m_count = 0u;
    usize m_head = 0u;
    u64 m_previousCount = 0u;
    Timer m_previousTime = {};
    bool m_started = false;
    u64 m_stalls = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
