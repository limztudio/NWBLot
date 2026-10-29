// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "presentation_fps_probe.h"
#include "smoke_environment.h"

#include <core/perf/session.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Buffered CPU and GPU scope publication probe for smoke tests.
class SmokeCpuGpuTimingProbe final{
private:
    static constexpr usize s_MaxScopesPerDomain = 512u;
    static constexpr usize s_MaxRecords = 262144u;


private:
    struct Scope{
        Name name = s_NameNone;
        u64 lastPublication = 0u;
        u64 eligibleSamples = 0u;
        u32 generation = 0u;
        b8 recorded = false;
    };

    struct Record{
        Core::Perf::TimingStats stats;
        u64 observationFrame = 0u;
        u64 presentations = 0u;
        u32 scope = 0u;
        u32 generation = 0u;
        b8 gpu = false;
    };


public:
    explicit SmokeCpuGpuTimingProbe(TStringView owner, Core::Alloc::GlobalArena& arena)noexcept;


public:
    [[nodiscard]] b8 initialize(b8 requested, b8 presentationTimingEnabled, AStringView outputPath);
    [[nodiscard]] b8 observe(const Core::Perf::Session& session, const PresentationFpsProbe& presentation, u64 successfulPresentations, b8 complete);
    [[nodiscard]] b8 hasCompletedGpuSamples(const Name& name, u64 minimumSamples)const noexcept;


private:
    [[nodiscard]] b8 capture(const Core::Perf::TimingView& timing, b8 gpu, u64 frame, u64 presentations);
    [[nodiscard]] b8 write(const PresentationFpsSample& presentation, u64 endFrame);


private:
    TStringView m_owner;
    SmokeEnvironmentString m_outputPath;
    Vector<Record, Core::Alloc::GlobalArena> m_records;
    Scope m_scopes[2u][s_MaxScopesPerDomain] = {};
    u64 m_firstSourceFrame = 0u;
    u64 m_firstPresentation = 0u;
    b8 m_enabled = false;
    b8 m_started = false;
    b8 m_complete = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

