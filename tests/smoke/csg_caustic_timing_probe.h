// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "presentation_fps_probe.h"
#include "smoke_cpu_gpu_timing_probe.h"

#include <loader/project_entry.h>

#include <core/graphics/runtime/render_pass.h>

#include <global/expected.h>
#include <global/filesystem/path.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CsgCausticTimingProbe final : public Core::IRenderPass{
private:
    struct Record{
        Core::Perf::TimingStats stats;
        u64 observation = 0u;
        u32 scope = 0u;
    };

    struct NativeEndpoint{
        Timer timestamp = {};
        u64 source = 0u;
        u64 presentations = 0u;
        u32 completeMask = 0u;
        u32 positiveMask = 0u;
        u32 beginMask = 0u;
        u32 endMask = 0u;
    };

    struct Interval{
        PresentationFpsSample presentation;
        u64 firstSource = 0u;
        u64 endSource = 0u;
    };


private:
    static constexpr usize s_GpuScopeCount = 6u;
    static constexpr usize s_CpuScopeCount = 5u;
    static constexpr usize s_ScopeCount = s_GpuScopeCount + s_CpuScopeCount;
    static constexpr usize s_MaxRecords = 262144u;
    static constexpr usize s_MaxIntervals = 2048u;
    static constexpr usize s_MaxNativeEndpoints = 131072u;
    static constexpr f64 s_ReportIntervalSeconds = 0.5;
    static constexpr u64 s_PreparationPresentations = 64u;
    static constexpr u64 s_MinimumPresentations = 100u;
    static constexpr u32 s_MinimumPositiveIntervals = 11u;
    static constexpr f64 s_MinimumMeasurementSeconds = 30.0;


public:
    explicit CsgCausticTimingProbe(ProjectRuntimeContext& context)noexcept;


public:
    [[nodiscard]] bool initialize(bool diagnostic, bool csg, bool hardware, AStringView outputPath, AStringView cpuOutputPath, AStringView startPath);
    [[nodiscard]] bool update();
    void finish();
    virtual bool shouldRenderUnfocused()noexcept override{ return true; }


private:
    [[nodiscard]] bool capture(const Core::Perf::TimingView& view, u64 sourceFrame, bool cpu);
    [[nodiscard]] Expected<bool> selectCommonWindow();
    [[nodiscard]] bool write(u64 collectionEndSource);


private:
    ProjectRuntimeContext& m_context;
    SmokeCpuGpuTimingProbe m_cpu;
    PresentationFpsProbe m_presentation{ 30.0 };
    Vector<Record, Core::Alloc::GlobalArena> m_records;
    Vector<NativeEndpoint, Core::Alloc::GlobalArena> m_native;
    Vector<Interval, Core::Alloc::GlobalArena> m_intervals;
    SmokeEnvironmentString m_output;
    ::Path<Core::Alloc::GlobalArena> m_startPath;
    u64 m_lastPublication[s_ScopeCount] = {};
    u32 m_generations[s_ScopeCount] = {};
    Timer m_preparationBegin = {};
    u64 m_firstSource = 0u;
    usize m_commonBegin = Limit<usize>::s_Max;
    usize m_commonEnd = 0u;
    PresentationFpsSample m_commonPresentation;
    u32 m_positiveIntervals = 0u;
    bool m_diagnostic = false;
    bool m_prepared = false;
    bool m_measuring = false;
    bool m_complete = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

