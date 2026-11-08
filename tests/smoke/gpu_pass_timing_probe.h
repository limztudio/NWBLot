// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "smoke_environment.h"

#include <tests/smoke/presentation_types.adhd.h>

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/perf/timing.h>

#include <global/basic_string.h>
#include <global/containers.h>
#include <global/filesystem.h>
#include <global/name.h>
#include <global/simplemath.h>
#include <global/type.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Normalize asynchronous GPU timing windows by dispatch count.
class GpuPassTimingProbe final{
private:
    static constexpr f64 s_WarmupSeconds = 0.25;
    static constexpr f64 s_ReportIntervalSeconds = 0.5;
    static constexpr f64 s_MillisecondsPerSecond = 1000.0;
    static constexpr i32 s_TimingFilePrecision = 4;
    static constexpr Name s_Arena = NWB::Tests::Smoke::s_SmokeNameSmokeGpuPassTimingProbe;


private:
    using ScopeAccum = GpuPassTimingScopeAccum;

    struct ScopeState{
        ScopeAccum interval;
        u64 lastFoldedPublish = 0u;
    };


public:
    explicit GpuPassTimingProbe(TStringView label)
        : m_arena(s_Arena)
        , m_scopes(m_arena)
        , m_label(label)
    {}


public:
    void recordFrame(f32 delta, const Core::Perf::TimingView& gpuTiming);


private:
    void accumulate(const Core::Perf::TimingView& gpuTiming);
    void report(const Core::Perf::TimingView& gpuTiming);
    void resetInterval()noexcept;


private:
    Core::Alloc::GlobalArena m_arena;
    Vector<ScopeState, Core::Alloc::GlobalArena> m_scopes;
    TStringView m_label = NWB_TEXT("Smoke");
    f64 m_elapsedSeconds = 0.0;
    f64 m_intervalSeconds = 0.0;
    u32 m_intervalFrames = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

