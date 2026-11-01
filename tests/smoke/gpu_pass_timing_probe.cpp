// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "gpu_pass_timing_probe.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_pass_timing_probe{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void OpenTimingFile(Core::Alloc::ScratchArena& scratchArena, OutputFileStream& timingFile){
    const auto timingPath = Tests::Smoke::ReadSmokeEnvironmentText(scratchArena, "NWB_GPU_TIMING_FILE");
    if(!timingPath)
        return;

    timingFile.open(timingPath->c_str(), s_FileOpenAppend);
    if(!timingFile.is_open())
        NWB_LOGGER_ERROR(NWB_TEXT("GpuPassTimingProbe: failed to open requested timing output"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuPassTimingProbe::recordFrame(const f32 delta, const Core::Perf::TimingView& gpuTiming){
    const f64 safeDelta = IsFinite(delta) ? Max(static_cast<f64>(delta), 0.0) : 0.0;
    if(safeDelta <= 0.0)
        return;

    m_elapsedSeconds += safeDelta;
    if(m_elapsedSeconds < s_WarmupSeconds)
        return;
    if(!gpuTiming.valid())
        return;

    m_intervalSeconds += safeDelta;
    ++m_intervalFrames;
    accumulate(gpuTiming);

    if(m_intervalSeconds < s_ReportIntervalSeconds)
        return;

    report(gpuTiming);
    resetInterval();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuPassTimingProbe::accumulate(const Core::Perf::TimingView& gpuTiming){
    const usize scopeCount = gpuTiming.scopeCount();
    if(m_scopes.size() < scopeCount)
        m_scopes.resize(scopeCount);
    for(usize i = 0u; i < scopeCount; ++i){
        const Core::Perf::TimingStats& stats = gpuTiming.statsAt(i);
        if(!stats.valid())
            continue;

        // Track the last folded GPU window index so resets never double-count.
        if(m_scopes[i].lastFoldedPublish == stats.publishFrameIndex)
            continue;
        m_scopes[i].lastFoldedPublish = stats.publishFrameIndex;

        ScopeAccum& accum = m_scopes[i].interval;
        if(accum.frames == 0u){
            accum.minSeconds = stats.seconds;
            accum.maxSeconds = stats.seconds;
        }
        else{
            accum.minSeconds = Min(accum.minSeconds, stats.seconds);
            accum.maxSeconds = Max(accum.maxSeconds, stats.seconds);
        }
        accum.sumSeconds += stats.seconds;
        accum.samples += stats.sampleCount;
        ++accum.frames;
    }
}

void GpuPassTimingProbe::report(const Core::Perf::TimingView& gpuTiming){
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("{}: gpu per-pass timing over {} frames / {}s"), m_label, m_intervalFrames, m_intervalSeconds);

    // Write per-pass timings to the optional file sink for A/B scripts.
    Core::Alloc::ScratchArena scratchArena(s_SmokeEnvironmentArena);
    OutputFileStream timingFile;
    __hidden_gpu_pass_timing_probe::OpenTimingFile(scratchArena, timingFile);
    if(timingFile.is_open()){
        timingFile.setf(s_FileFormatFixed, s_FileFormatFloatField);
        timingFile.precision(s_TimingFilePrecision);
        timingFile << "=== interval: " << m_intervalFrames << " frames / " << m_intervalSeconds << "s ===\n";
    }

    const usize scopeCount = gpuTiming.scopeCount();
    for(usize i = 0u; i < scopeCount; ++i){
        const ScopeAccum& accum = m_scopes[i].interval;
        if(accum.frames == 0u)
            continue;

        const Name scopeName = gpuTiming.scopeNameAt(i);
        const f64 averageSeconds = accum.sumSeconds / static_cast<f64>(accum.frames);
        const f64 avgMs = averageSeconds * s_MillisecondsPerSecond;
        const f64 minMs = accum.minSeconds * s_MillisecondsPerSecond;
        const f64 maxMs = accum.maxSeconds * s_MillisecondsPerSecond;
        const f64 totalMs = accum.sumSeconds * s_MillisecondsPerSecond;
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("  {}: gpu_window_ms avg={} min={} max={} published_windows={}"),
            ConvertString(scopeName.resolvedText()),
            avgMs,
            minMs,
            maxMs,
            accum.frames
        );
        if(timingFile.is_open()){
            timingFile
                << "  " << scopeName.resolvedText()
                << ": window_avg_ms=" << avgMs
                << " window_min_ms=" << minMs
                << " window_max_ms=" << maxMs
                << " published_windows=" << accum.frames
                << " total_ms=" << totalMs
                << " gpu_samples=" << accum.samples
                << " sample_avg_ms=" << (totalMs / static_cast<f64>(accum.samples))
                << "\n"
            ;
        }
    }
    if(timingFile.is_open()){
        timingFile.close();
        if(!timingFile.good())
            NWB_LOGGER_ERROR(NWB_TEXT("GpuPassTimingProbe: timing output write or close failed"));
    }
}

void GpuPassTimingProbe::resetInterval()noexcept{
    m_intervalSeconds = 0.0;
    m_intervalFrames = 0u;
    for(ScopeState& scope : m_scopes)
        scope.interval = ScopeAccum{};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

