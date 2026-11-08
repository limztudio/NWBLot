// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <tests/smoke/presentation_types.adhd.h>

#include <core/common/global.h>

#include <global/simd/vector.h>
#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

// Measure FPS from accepted presentation counts and steady-clock wall time.
class PresentationFpsProbe final{
public:
    explicit PresentationFpsProbe(f64 warmupSeconds = 0.25, f64 measurementSeconds = 0.0)noexcept;


public:
    [[nodiscard]] PresentationFpsStatus::Enum observe(u64 successfulPresentations, Timer now)noexcept;
    [[nodiscard]] const PresentationFpsSample& interval()const noexcept{ return m_interval; }

    [[nodiscard]] const PresentationFpsSample& total()const noexcept{ return m_total; }

    [[nodiscard]] b8 measurementStarted()const noexcept{ return m_measuring; }


private:
    PresentationFpsSample m_interval;
    PresentationFpsSample m_total;
    f64 m_warmupSeconds = 0.25;
    f64 m_measurementSeconds = 0.0;
    Timer m_warmupBegin = {};
    Timer m_measurementBegin = {};
    Timer m_intervalBegin = {};
    Timer m_previousObservation = {};
    u64 m_previousPresentationCount = 0u;
    u64 m_intervalPresentationCount = 0u;
    b8 m_started = false;
    b8 m_measuring = false;
    b8 m_complete = false;
    b8 m_invalid = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

