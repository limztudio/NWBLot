// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "smoke_environment.h"

#include <core/common/log.h>
#include <core/perf/timing.h>
#include <global/basic_string.h>
#include <global/containers.h>
#include <global/filesystem.h>
#include <global/name.h>
#include <global/math/vector_double.h>
#include <global/simplemath.h>
#include <global/type.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// GPU timing is async: windows != frames. Window statistics describe published totals; total_ms/gpu_samples normalize by dispatches, not cadence.
class GpuPassTimingProbe final{
private:
    static constexpr f64 s_WarmupSeconds = 0.25;
    static constexpr f64 s_ReportIntervalSeconds = 0.5;
    static constexpr f64 s_MaxMeasuredFrameSeconds = 0.25;
    static constexpr f64 s_MillisecondsPerSecond = 1000.0;
    static constexpr int s_TimingFilePrecision = 4;
    static constexpr Name s_Arena{ "tests/smoke/gpu_pass_timing_probe" };


private:
    struct ScopeAccum{
        f64 sumSeconds = 0.0;
        f64 minSeconds = 0.0;
        f64 maxSeconds = 0.0;
        u64 samples = 0u;
        u32 frames = 0u;
    };

    struct ScopeState{
        ScopeAccum interval;
        u64 lastFoldedPublish = 0u;
    };


private:
    static void OpenTimingFile(OutputFileStream& timingFile){
        Core::Alloc::GlobalArena arena(s_SmokeEnvironmentArena);
        const auto timingPath = ReadSmokeEnvironmentText(arena, "NWB_GPU_TIMING_FILE");
        if(!timingPath)
            return;

        timingFile.open(timingPath->c_str(), s_FileOpenAppend);
    }


public:
    explicit GpuPassTimingProbe(TStringView label)
        : m_arena(s_Arena)
        , m_scopes(m_arena)
        , m_label(label)
    {}


public:
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
            return;

        report(gpuTiming);
        resetInterval();
    }


private:
    void accumulate(const Core::Perf::TimingView& gpuTiming){
        const usize scopeCount = gpuTiming.scopeCount();
        if(m_scopes.size() < scopeCount)
            m_scopes.resize(scopeCount);
        for(usize i = 0u; i < scopeCount; ++i){
            const Core::Perf::TimingStats& stats = gpuTiming.statsAt(i);
            if(!stats.valid())
                continue;

            // Fold each GPU window once: persistent watermark survives resets, so straddling windows never double-count.
            // Publish indices > 0 post-warmup, so 0 means none folded yet.
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

    void report(const Core::Perf::TimingView& gpuTiming){
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("{}: gpu per-pass timing over {} frames / {}s")
            , m_label
            , m_intervalFrames
            , m_intervalSeconds
        );

        // GUI logging is not scrapeable; the optional file sink exposes per-pass A/B timings.
        OutputFileStream timingFile;
        OpenTimingFile(timingFile);
        if(timingFile.is_open()){
            timingFile.setf(s_FileFormatFixed, s_FileFormatFloatField);
            timingFile.precision(s_TimingFilePrecision);
            timingFile << "=== interval: " << static_cast<unsigned>(m_intervalFrames) << " frames / " << m_intervalSeconds << "s ===\n";
        }

        const usize scopeCount = gpuTiming.scopeCount();
        for(usize i = 0u; i < scopeCount; ++i){
            const ScopeAccum& accum = m_scopes[i].interval;
            if(accum.frames == 0u)
                continue;

            const Name scopeName = gpuTiming.scopeNameAt(i);
            const SIMDVectorDouble totalsMs = SIMDVectorDouble{
                accum.sumSeconds / static_cast<f64>(accum.frames), accum.sumSeconds
            } * s_MillisecondsPerSecond;
            const SIMDVectorDouble rangeMs = SIMDVectorDouble{ accum.minSeconds, accum.maxSeconds } * s_MillisecondsPerSecond;
            NWB_LOGGER_ESSENTIAL_INFO(
                NWB_TEXT("  {}: gpu_window_ms avg={} min={} max={} published_windows={}")
                , StringConvert(scopeName.resolvedText())
                , totalsMs.x
                , rangeMs.x
                , rangeMs.y
                , accum.frames
            );
            if(timingFile.is_open()){
                timingFile
                    << "  " << scopeName.resolvedText()
                    << ": window_avg_ms=" << totalsMs.x
                    << " window_min_ms=" << rangeMs.x
                    << " window_max_ms=" << rangeMs.y
                    << " published_windows=" << static_cast<unsigned>(accum.frames)
                    << " total_ms=" << totalsMs.y
                    << " gpu_samples=" << accum.samples
                    << " sample_avg_ms=" << totalsMs.y / static_cast<f64>(accum.samples)
                    << '\n'
                ;
            }
        }
    }

    void resetInterval(){
        m_intervalSeconds = 0.0;
        m_intervalFrames = 0u;
        for(ScopeState& scope : m_scopes)
            scope.interval = ScopeAccum{};
    }


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

