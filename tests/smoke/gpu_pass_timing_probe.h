// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "smoke_environment.h"

#include <core/common/log.h>
#include <core/perf/timing.h>
#include <global/basic_string.h>
#include <global/filesystem.h>
#include <global/name.h>
#include <global/simplemath.h>
#include <global/type.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Published GPU timing is asynchronous: a window can contain samples from several source frames or only part of
// one frame. Legacy avg/min/max fields describe totals per published window. The file sink also exports total_ms,
// gpu_samples and sample_avg_ms so benchmarks can normalize by actual dispatches instead of publication cadence.
// A multi-dispatch pass must account for its known dispatches per frame; window counts are not frame counts.
class GpuPassTimingProbe final{
private:
    static constexpr f64 s_WarmupSeconds = 0.25;
    static constexpr f64 s_ReportIntervalSeconds = 0.5;
    static constexpr f64 s_MaxMeasuredFrameSeconds = 0.25;
    static constexpr f64 s_MillisecondsPerSecond = 1000.0;
    static constexpr int s_TimingFilePrecision = 4;
    static constexpr usize s_MaxScopes = 64u;

    struct ScopeAccum{
        f64 sumSeconds = 0.0;
        f64 minSeconds = 0.0;
        f64 maxSeconds = 0.0;
        u32 frames = 0u;
        u64 samples = 0u;
    };


public:
    explicit GpuPassTimingProbe(const tchar* label)
        : m_label(label)
    {}

    void recordFrame(const f32 delta, const Core::Perf::TimingView& gpuTiming){
        const f64 safeDelta = IsFinite(delta) && delta > 0.0f ? static_cast<f64>(delta) : 0.0;
        if(safeDelta <= 0.0)
            return;
        if(safeDelta > s_MaxMeasuredFrameSeconds)
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
            return; // m_intervalFrames is always >= 1 here (incremented unconditionally above)

        report(gpuTiming);
        resetInterval();
    }


private:
    static void OpenTimingFile(OutputFileStream& timingFile){
        Core::Alloc::GlobalArena arena(s_SmokeEnvironmentArena);
        SmokeEnvironmentString timingPath(arena);
        if(!ReadSmokeEnvironmentText("NWB_GPU_TIMING_FILE", timingPath))
            return;

        timingFile.open(timingPath.c_str(), s_FileOpenAppend);
    }

    void accumulate(const Core::Perf::TimingView& gpuTiming){
        const usize scopeCount = Min(gpuTiming.scopeCount(), s_MaxScopes);
        for(usize i = 0u; i < scopeCount; ++i){
            const Core::Perf::TimingStats& stats = gpuTiming.statsAt(i);
            if(!stats.valid())
                continue;

            // Fold each published GPU window at most once. The watermark PERSISTS across interval resets, so a window
            // that straddles a report boundary (onUpdate ran but no new GPU publish yet, e.g. an occluded/skipped
            // frame still returns the prior window) is never double-counted into the next interval. Publish indices
            // strictly increase and are > 0 once accumulation begins (post-warmup), so the 0 default reads as "none
            // folded yet" without colliding with a real window.
            if(m_scopeLastFoldedPublish[i] == stats.publishFrameIndex)
                continue;
            m_scopeLastFoldedPublish[i] = stats.publishFrameIndex;

            ScopeAccum& accum = m_scopes[i];
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

    void report(const Core::Perf::TimingView& gpuTiming){
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("{}: gpu per-pass timing over {} frames / {}s")
            , m_label
            , m_intervalFrames
            , m_intervalSeconds
        );

        // Optional file sink for bounded profiling A/B: the smoke app is a GUI process with no stdout, and its logger routes
        // to the logserver's WINDOW, so an automated harness cannot scrape these numbers. When NWB_GPU_TIMING_FILE is set,
        // ALSO append each interval's per-pass averages there (Name::c_str() is the readable scope text in a dbg build).
        OutputFileStream timingFile;
        OpenTimingFile(timingFile);
        if(timingFile.is_open()){
            timingFile.setf(s_FileFormatFixed, s_FileFormatFloatField);
            timingFile.precision(s_TimingFilePrecision);
            timingFile << "=== interval: " << static_cast<unsigned>(m_intervalFrames) << " frames / " << m_intervalSeconds << "s ===\n";
        }

        const usize scopeCount = Min(gpuTiming.scopeCount(), s_MaxScopes);
        for(usize i = 0u; i < scopeCount; ++i){
            const ScopeAccum& accum = m_scopes[i];
            if(accum.frames == 0u)
                continue;

            const Name scopeName = gpuTiming.scopeNameAt(i);
            const f64 averageMs = (accum.sumSeconds / static_cast<f64>(accum.frames)) * s_MillisecondsPerSecond;
            NWB_LOGGER_ESSENTIAL_INFO(
                NWB_TEXT("  {}: gpu_window_ms avg={} min={} max={} published_windows={}")
                , StringConvert(scopeName.c_str())
                , averageMs
                , accum.minSeconds * s_MillisecondsPerSecond
                , accum.maxSeconds * s_MillisecondsPerSecond
                , accum.frames
            );
            if(timingFile.is_open()){
                timingFile
                    << "  " << scopeName.c_str()
                    << ": avg=" << averageMs
                    << " min=" << accum.minSeconds * s_MillisecondsPerSecond
                    << " max=" << accum.maxSeconds * s_MillisecondsPerSecond
                    << " samples=" << static_cast<unsigned>(accum.frames)
                    << " total_ms=" << accum.sumSeconds * s_MillisecondsPerSecond
                    << " gpu_samples=" << accum.samples
                    << " sample_avg_ms=" << accum.sumSeconds * s_MillisecondsPerSecond / static_cast<f64>(accum.samples)
                    << '\n'
                ;
            }
        }
    }

    void resetInterval(){
        m_intervalSeconds = 0.0;
        m_intervalFrames = 0u;
        for(ScopeAccum& accum : m_scopes)
            accum = ScopeAccum{};
    }


private:
    const tchar* m_label = NWB_TEXT("Smoke");
    f64 m_elapsedSeconds = 0.0;
    f64 m_intervalSeconds = 0.0;
    u32 m_intervalFrames = 0u;
    ScopeAccum m_scopes[s_MaxScopes] = {};
    // Per-scope last-folded publish-frame watermark. NOT reset between intervals (resetInterval only clears
    // m_scopes), so a GPU window folded in one interval is never re-folded into the next at the boundary.
    u64 m_scopeLastFoldedPublish[s_MaxScopes] = {};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

