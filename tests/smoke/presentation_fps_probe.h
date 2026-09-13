// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/common/global.h>

#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace PresentationFpsStatus{
    enum Enum : u8{
        Waiting,
        Interval,
        Complete,
        Invalid,
    };
};

struct PresentationFpsSample{
    u64 firstPresentationCount = 0u;
    u64 lastPresentationCount = 0u;
    f64 wallSeconds = 0.0;

    [[nodiscard]] u64 presentations()const noexcept{ return lastPresentationCount - firstPresentationCount; }
    [[nodiscard]] f64 averageFps()const noexcept{ return wallSeconds > 0.0 ? presentations() / wallSeconds : 0.0; }
};

// Observes an owning runtime's cumulative accepted-present count against steady-clock timestamps. Repeated idle
// callbacks do not add frames; long stalls remain in elapsed wall time. No simulation-delta or GPU-query input.
class PresentationFpsProbe final{
public:
    explicit PresentationFpsProbe(f64 warmupSeconds = 0.25, f64 measurementSeconds = 0.0);


public:
    [[nodiscard]] PresentationFpsStatus::Enum observe(u64 successfulPresentations, Timer now);
    [[nodiscard]] const PresentationFpsSample& interval()const noexcept{ return m_interval; }
    [[nodiscard]] const PresentationFpsSample& total()const noexcept{ return m_total; }


private:
    f64 m_warmupSeconds = 0.25;
    f64 m_measurementSeconds = 0.0;
    Timer m_warmupBegin = {};
    Timer m_measurementBegin = {};
    Timer m_intervalBegin = {};
    Timer m_previousObservation = {};
    u64 m_previousPresentationCount = 0u;
    u64 m_intervalPresentationCount = 0u;
    PresentationFpsSample m_interval;
    PresentationFpsSample m_total;
    bool m_started = false;
    bool m_measuring = false;
    bool m_complete = false;
    bool m_invalid = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

