// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_cpu_timing_probe.h"

#include "smoke_environment.h"

#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/reflection/timing_names.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ReflectionCpuTimingProbe::ReflectionCpuTimingProbe(ProjectRuntimeContext& context)
    : m_context(context)
    , m_publications(NWB_TEXT("reflection"), context.objectArena)
{}

bool ReflectionCpuTimingProbe::initialize(const AStringView caseName){
    if((caseName != "optical_csg_cap" && caseName != "optical_csg_reference") || !m_context.requestQuit){
        NWB_LOGGER_ERROR(NWB_TEXT("ReflectionCpuTiming: requires the cap or matched slab fixture and a quit callback"));
        return false;
    }
    const auto outputPath = ReadSmokeEnvironmentText(m_context.objectArena, "NWB_REFLECTION_CPU_GPU_TIMING_FILE");
    if(!m_publications.initialize(
        true, true, outputPath ? AStringView(outputPath->data(), outputPath->size()) : AStringView{}
    ))
        return false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionCpuTiming: configured case={} preparation_presentations=64 warmup_seconds=5")
        NWB_TEXT(" minimum_seconds=30 minimum_presentations=100 minimum_positive_intervals=11 diagnostic_only=1")
        , StringConvert(caseName)
    );
    return true;
}

bool ReflectionCpuTimingProbe::update(){
    if(m_complete)
        return true;
    const u64 presentations = m_context.graphics.getSuccessfulPresentationCount();
    const u64 sourceFrame = m_context.perfSession.frameIndex();
    const Timer now = TimerNow();
    if(!m_prepared){
        if(presentations < s_PreparationPresentations)
            return true;
        m_prepared = true;
        m_preparationBegin = now;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionCpuTiming: preparation complete presentations={} source_frame={}")
            , presentations
            , sourceFrame
        );
    }
    const auto status = m_presentation.observe(presentations, now);
    if(status == PresentationFpsStatus::Invalid){
        NWB_LOGGER_ERROR(NWB_TEXT("ReflectionCpuTiming: presentation counter or steady clock regressed"));
        return false;
    }
    if(m_presentation.measurementStarted() && !m_measuring){
        m_measuring = true;
        m_intervalSourceFrame = sourceFrame;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionCpuTiming: measurement begin presentations={} source_frame={} warmup_seconds={}")
            , presentations
            , sourceFrame
            , DurationInSeconds<f64>(now, m_preparationBegin)
        );
    }
    if(!m_publications.observe(m_context.perfSession, m_presentation, presentations, false))
        return false;
    if(status != PresentationFpsStatus::Interval)
        return true;
    const auto& interval = m_presentation.interval();
    if(interval.presentations() > 0u)
        ++m_positiveIntervals;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionCpuTiming: interval presentations={} seconds={} first={} last={}")
        NWB_TEXT(" first_source={} end_source={}")
        , interval.presentations()
        , interval.wallSeconds
        , interval.firstPresentationCount
        , interval.lastPresentationCount
        , m_intervalSourceFrame
        , sourceFrame
    );
    m_intervalSourceFrame = sourceFrame;
    const auto& total = m_presentation.total();
    if(
        total.wallSeconds < s_MinimumMeasurementSeconds || total.presentations() < s_MinimumPresentations
        || m_positiveIntervals < s_MinimumPositiveIntervals
        || !m_publications.hasCompletedGpuSamples(Impl::RendererGpuTimingScope::s_Frame.identity, s_MinimumPresentations)
        || !m_publications.hasCompletedGpuSamples(Impl::ReflectionGpuTimingScope::s_Hardware.identity, s_MinimumPresentations)
    )
        return true;
    if(!m_publications.observe(m_context.perfSession, m_presentation, presentations, true))
        return false;
    m_complete = true;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionCpuTiming: measurement complete presentations={} seconds={} first={} last={}")
        NWB_TEXT(" positive_intervals={} end_source={} diagnostic_only=1")
        , total.presentations()
        , total.wallSeconds
        , total.firstPresentationCount
        , total.lastPresentationCount
        , m_positiveIntervals
        , sourceFrame
    );
    m_context.requestQuit();
    return true;
}

void ReflectionCpuTimingProbe::finish(){
    if(!m_complete)
        NWB_LOGGER_ERROR(NWB_TEXT("ReflectionCpuTiming: shutdown before the diagnostic measurement completed"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

