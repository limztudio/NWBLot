// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "presentation_fps_probe.h"
#include "smoke_cpu_gpu_timing_probe.h"

#include <loader/project_entry.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ReflectionCpuTimingProbe final{
private:
    static constexpr u64 s_PreparationPresentations = 64u;
    static constexpr u64 s_MinimumPresentations = 100u;
    static constexpr u32 s_MinimumPositiveIntervals = 11u;
    static constexpr f64 s_MinimumMeasurementSeconds = 30.0;


public:
    explicit ReflectionCpuTimingProbe(ProjectRuntimeContext& context);


public:
    [[nodiscard]] bool initialize(AStringView caseName);
    [[nodiscard]] bool update();
    void finish();


private:
    ProjectRuntimeContext& m_context;
    SmokeCpuGpuTimingProbe m_publications;
    PresentationFpsProbe m_presentation{ 5.0 };
    Timer m_preparationBegin = {};
    u64 m_intervalSourceFrame = 0u;
    u32 m_positiveIntervals = 0u;
    bool m_prepared = false;
    bool m_measuring = false;
    bool m_complete = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

