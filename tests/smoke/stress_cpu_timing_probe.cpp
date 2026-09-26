// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "stress_cpu_timing_probe.h"

#include <core/common/log.h>
#include <global/filesystem.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


StressCpuTimingProbe::StressCpuTimingProbe(Core::Alloc::GlobalArena& arena)
    : m_outputPath(arena)
    , m_records(arena)
{}

bool StressCpuTimingProbe::initialize(const bool requested, const bool presentationTimingEnabled){
    const bool hasOutput = ReadSmokeEnvironmentText("NWB_STRESS_CPU_TIMING_FILE", m_outputPath);
    if(hasOutput != requested || (requested && !presentationTimingEnabled)){
        NWB_LOGGER_ERROR(NWB_TEXT("StressCpuTimingProbe: diagnostics require presentation timing and an explicit output path"));
        return false;
    }
    m_enabled = requested;
    if(!m_enabled)
        return true;
    // Reserve before warmup: no publication rows allocate or write files inside the measured window.
    m_records.reserve(s_MaxRecords);
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressCpuTimingProbe: enabled cpu=1 gpu=1 memory=0 diagnostic_only=1"));
    return true;
}

bool StressCpuTimingProbe::observe(
    const Core::Perf::Session& session,
    const PresentationFpsProbe& presentation,
    const u64 successfulPresentations,
    const bool complete){
    if(!m_enabled || m_complete || !presentation.measurementStarted())
        return true;
    const Core::Perf::CaptureOptions options = session.captureOptions();
    if(!options.cpuTimingActive() || !options.gpuTimingActive() || options.memoryActive()){
        NWB_LOGGER_ERROR(NWB_TEXT("StressCpuTimingProbe: active capture options disagree with requested diagnostics"));
        return false;
    }
    if(!m_started){
        m_firstSourceFrame = session.frameIndex();
        m_firstPresentation = successfulPresentations;
        m_started = true;
    }
    if(!capture(session.cpuTimingView(), false, session.frameIndex(), successfulPresentations))
        return false;
    if(!capture(session.gpuTimingView(), true, session.frameIndex(), successfulPresentations))
        return false;
    if(!complete)
        return true;
    // The presentation probe sampled its final steady-clock timestamp before this potentially slow file write.
    if(!write(presentation.total(), session.frameIndex()))
        return false;
    m_complete = true;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("StressCpuTimingProbe: complete records={} first={} last={} first_source={} end_source={}")
        , static_cast<u64>(m_records.size())
        , m_firstPresentation
        , successfulPresentations
        , m_firstSourceFrame
        , session.frameIndex()
    );
    return true;
}

bool StressCpuTimingProbe::capture(
    const Core::Perf::TimingView& timing,
    const bool gpu,
    const u64 frame,
    const u64 presentations){
    if(!timing.valid() || timing.scopeCount() > s_MaxScopesPerDomain){
        NWB_LOGGER_ERROR(NWB_TEXT("StressCpuTimingProbe: timing view invalid or scope capacity exceeded"));
        return false;
    }
    for(usize index = 0u; index < timing.scopeCount(); ++index){
        const Core::Perf::TimingStats& stats = timing.statsAt(index);
        if(!stats.valid())
            continue;
        Scope& scope = m_scopes[gpu ? 1u : 0u][index];
        const Core::Perf::TimingScopeId id = timing.scopeAt(index);
        const Name name = timing.scopeNameAt(index);
        if(scope.recorded && (scope.generation != id.generation || scope.name != name)){
            NWB_LOGGER_ERROR(NWB_TEXT("StressCpuTimingProbe: scope identity changed inside measurement"));
            return false;
        }
        if(scope.recorded && scope.lastPublication == stats.publishFrameIndex)
            continue;
        if((scope.recorded && stats.publishFrameIndex < scope.lastPublication) || m_records.size() >= s_MaxRecords){
            NWB_LOGGER_ERROR(NWB_TEXT("StressCpuTimingProbe: publication regressed or record capacity exceeded"));
            return false;
        }
        scope.name = name;
        scope.generation = id.generation;
        scope.lastPublication = stats.publishFrameIndex;
        scope.recorded = true;
        m_records.push_back({ stats, frame, presentations, static_cast<u32>(index), gpu });
    }
    return true;
}

bool StressCpuTimingProbe::write(const PresentationFpsSample& presentation, const u64 endFrame){
    if(presentation.firstPresentationCount != m_firstPresentation || endFrame <= m_firstSourceFrame){
        NWB_LOGGER_ERROR(NWB_TEXT("StressCpuTimingProbe: presentation and source-frame boundaries disagree"));
        return false;
    }
    OutputFileStream output(m_outputPath.c_str(), s_FileOpenTruncate);
    if(!output.is_open()){
        NWB_LOGGER_ERROR(NWB_TEXT("StressCpuTimingProbe: failed to open diagnostic output"));
        return false;
    }
    output.precision(17);
    output << "NWB_STRESS_CPU_GPU_DIAGNOSTIC 1\n";
    output << "capture cpu=1 gpu=1 memory=0 diagnostic_only=1\n";
    output << "window " << presentation.firstPresentationCount << ' ' << presentation.lastPresentationCount
        << ' ' << m_firstSourceFrame << ' ' << endFrame << ' ' << presentation.wallSeconds << '\n'
    ;
    usize scopeCount = 0u;
    for(usize domain = 0u; domain < 2u; ++domain){
        for(usize index = 0u; index < s_MaxScopesPerDomain; ++index){
            const Scope& scope = m_scopes[domain][index];
            if(!scope.recorded)
                continue;
            output << "scope " << domain << ' ' << index << ' ' << scope.name.c_str() << '\n';
            ++scopeCount;
        }
    }
    for(const Record& record : m_records){
        const Core::Perf::TimingStats& stats = record.stats;
        output << "sample " << (record.gpu ? 1u : 0u) << ' ' << record.scope << ' ' << record.observationFrame
            << ' ' << record.presentations << ' ' << stats.publishFrameIndex
            << ' ' << stats.firstSampleFrameIndex << ' ' << stats.lastSampleFrameIndex << ' ' << stats.sampleCount
            << ' ' << stats.seconds << ' ' << stats.minSeconds << ' ' << stats.maxSeconds << ' ' << stats.lastSeconds << '\n'
        ;
    }
    output << "complete " << m_records.size() << ' ' << scopeCount << '\n';
    output.flush();
    if(!output.good()){
        NWB_LOGGER_ERROR(NWB_TEXT("StressCpuTimingProbe: diagnostic output write failed"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

