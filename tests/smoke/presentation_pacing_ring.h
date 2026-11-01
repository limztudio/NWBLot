// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/common/global.h>

#include <global/algorithm.h>
#include <global/simd/vector.h>
#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Track frame intervals and count idle stalls of 50ms or more once per gap.
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
    void reset()noexcept;
    void record(u64 successfulPresentations, Timer now)noexcept;
    [[nodiscard]] PresentationPacingSummary summarize()const;

    [[nodiscard]] usize sampleCount()const noexcept{ return m_count; }

    [[nodiscard]] u64 stallCount()const noexcept{ return m_stalls; }


private:
    Array<PresentationPacingSample, s_Capacity> m_samples{};
    usize m_count = 0u;
    usize m_head = 0u;
    u64 m_previousCount = 0u;
    u64 m_stalls = 0u;
    Timer m_previousObservationTime = {};
    Timer m_previousPresentationTime = {};
    b8 m_started = false;
    b8 m_pendingStallCounted = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

