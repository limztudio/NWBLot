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


// Smoke-only buffered CPU/GPU publication evidence. Overlapping scopes are not additive wall-time components.
class StressCpuTimingProbe final{
private:
    static constexpr usize s_MaxScopesPerDomain = 512u;
    static constexpr usize s_MaxRecords = 262144u;

    struct Scope{
        Name name = NAME_NONE;
        u64 lastPublication = 0u;
        u32 generation = 0u;
        bool recorded = false;
    };

    struct Record{
        Core::Perf::TimingStats stats;
        u64 observationFrame = 0u;
        u64 presentations = 0u;
        u32 scope = 0u;
        bool gpu = false;
    };


public:
    explicit StressCpuTimingProbe(Core::Alloc::GlobalArena& arena);


public:
    [[nodiscard]] bool initialize(bool requested, bool presentationTimingEnabled);
    [[nodiscard]] bool observe(
        const Core::Perf::Session& session,
        const PresentationFpsProbe& presentation,
        u64 successfulPresentations,
        bool complete
    );


private:
    [[nodiscard]] bool capture(const Core::Perf::TimingView& timing, bool gpu, u64 frame, u64 presentations);
    [[nodiscard]] bool write(const PresentationFpsSample& presentation, u64 endFrame);


private:
    SmokeEnvironmentString m_outputPath;
    Vector<Record, Core::Alloc::GlobalArena> m_records;
    Scope m_scopes[2u][s_MaxScopesPerDomain] = {};
    u64 m_firstSourceFrame = 0u;
    u64 m_firstPresentation = 0u;
    bool m_enabled = false;
    bool m_started = false;
    bool m_complete = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

