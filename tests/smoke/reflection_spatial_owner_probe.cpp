// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_spatial_owner_probe.h"

#include <impl/ecs_render/module.h>
#include <impl/ecs_render/reflection/timing_names.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ReflectionSpatialOwnerProbe::ReflectionSpatialOwnerProbe(ProjectRuntimeContext& context, Impl::RendererSystem& renderer)
    : m_context(context)
    , m_renderer(renderer)
{}

bool ReflectionSpatialOwnerProbe::configure(const AStringView selection, Impl::ReflectionSettings& settings){
    if(selection == "fresh1")
        m_radii[0] = 1u;
    else if(selection == "fresh2")
        m_radii[0] = 2u;
    else if(selection == "fresh3")
        m_radii[0] = 3u;
    else if(selection == "sequence3")
        m_lastPhase = 1u;
    else if(selection == "sequence2")
        m_lastPhase = 2u;
    else if(selection == "sequence1")
        m_lastPhase = 3u;
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSpatialOwner: unknown selection"));
        return false;
    }
    if(
        settings.traceMode != Impl::ReflectionTraceMode::Hardware || !settings.diagnosticsEnabled
        || !settings.temporalEnabled || settings.temporalMaxSamples != 1u
        || !settings.spatialFilterEnabled || settings.screenFeedbackEnabled
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSpatialOwner: requires hardware, diagnostics, spatial, temporal cap1 and feedback off"));
        return false;
    }
    settings.spatialRadius = m_radii[0];
    settings.samplingSeed = 99u;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSpatialOwner: selection={} initial_radius={} final_radius={} phases={}")
        , StringConvert(selection), m_radii[0], m_radii[m_lastPhase], m_lastPhase + 1u
    );
    return true;
}

bool ReflectionSpatialOwnerProbe::update(
    const Impl::ReflectionStatistics& statistics,
    const Core::Perf::TimingView& timing,
    Impl::ReflectionSettings& settings){
    if(!observeTiming(timing))
        return false;
    if(m_finalReset || statistics.sequence == 0u || !statistics.hardwareReady || !statistics.historyEligible)
        return true;
    if(!m_started){
        if(statistics.frameIndex < 3u)
            return true;
        return beginPhase(settings);
    }
    const u32 phaseSeed = 101u + m_phase;
    if(
        statistics.samplingSeed != phaseSeed || statistics.historyStartGraphicsFrame != m_phaseSource
        || statistics.historySampleCount != 1u || statistics.sampleIndex < 2u
        || !m_phaseWork.valid() || statistics.graphicsFrameIndex < m_phaseWork.lastSampleFrameIndex
    )
        return true;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSpatialOwnerWarm: index={} sequence={} generation={} graphics_frame={} epoch={}")
        NWB_TEXT(" start_graphics_frame={} sample_index={} seed={} work_publish={} work_first={} work_last={} work_samples={}")
        , m_phase, statistics.sequence, statistics.generation, statistics.graphicsFrameIndex, statistics.historyEpoch
        , statistics.historyStartGraphicsFrame, statistics.sampleIndex, statistics.samplingSeed
        , m_phaseWork.publishFrameIndex, m_phaseWork.firstSampleFrameIndex, m_phaseWork.lastSampleFrameIndex, m_phaseWork.sampleCount
    );
    if(m_phase < m_lastPhase){
        ++m_phase;
        return beginPhase(settings);
    }
    settings.samplingSeed = 0u;
    if(!m_renderer.setReflectionSettings(settings)){
        NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSpatialOwner: final seed reset rejected"));
        return false;
    }
    m_finalSource = m_context.graphics.getFrameIndex();
    m_finalReset = true;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSpatialOwnerCapture: radius={} graphics_frame={} seed=0")
        , settings.spatialRadius, m_finalSource
    );
    return true;
}

bool ReflectionSpatialOwnerProbe::canFinish(const Impl::ReflectionStatistics& statistics, const u64 capturedSource)const{
    return
        m_finalReset && capturedSource == m_finalSource && m_captureWork.valid()
        && statistics.hardwareReady && statistics.historyEligible && statistics.samplingSeed == 0u
        && statistics.historyStartGraphicsFrame == m_finalSource
        && statistics.graphicsFrameIndex >= Max(capturedSource, m_captureWork.lastSampleFrameIndex)
    ;
}

bool ReflectionSpatialOwnerProbe::observeTiming(const Core::Perf::TimingView& timing){
    const Core::Perf::TimingStats& work = timing.stats(Impl::ReflectionGpuTimingScope::s_Spatial.identity);
    if(!work.valid() || (m_hasPublication && work.publishFrameIndex == m_lastPublish))
        return true;
    if(
        (m_hasPublication && work.publishFrameIndex < m_lastPublish)
        || work.firstSampleFrameIndex > work.lastSampleFrameIndex
        || work.sampleCount > work.lastSampleFrameIndex - work.firstSampleFrameIndex + 1u
        || !IsFinite(work.seconds) || work.seconds < 0.0
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSpatialOwner: invalid completed spatial window"));
        return false;
    }
    m_hasPublication = true;
    m_lastPublish = work.publishFrameIndex;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSpatialOwnerWork: publish={} first={} last={} samples={}")
        , work.publishFrameIndex, work.firstSampleFrameIndex, work.lastSampleFrameIndex, work.sampleCount
    );
    if(m_started && !m_finalReset && !m_phaseWork.valid() && work.firstSampleFrameIndex >= m_phaseSource)
        m_phaseWork = work;
    // There is exactly one spatial range per rendered frame. A dense inclusive window proves the captured source
    // owns a completed range even when several source frames are published together.
    if(
        m_finalReset && work.firstSampleFrameIndex <= m_finalSource && work.lastSampleFrameIndex >= m_finalSource
        && work.sampleCount == work.lastSampleFrameIndex - work.firstSampleFrameIndex + 1u
    )
        m_captureWork = work;
    return true;
}

bool ReflectionSpatialOwnerProbe::beginPhase(Impl::ReflectionSettings& settings){
    settings.spatialRadius = m_radii[m_phase];
    settings.samplingSeed = 101u + m_phase;
    if(!m_renderer.setReflectionSettings(settings)){
        NWB_LOGGER_ERROR(NWB_TEXT("ReflectionSpatialOwner: radius transition rejected"));
        return false;
    }
    m_phaseSource = m_context.graphics.getFrameIndex();
    m_phaseWork = {};
    m_started = true;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ReflectionSpatialOwnerPhase: index={} radius={} seed={} graphics_frame={}")
        , m_phase, settings.spatialRadius, settings.samplingSeed, m_phaseSource
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

