// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "smoke_environment.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/perf/timing.h>

#include <global/basic_string.h>
#include <global/containers.h>
#include <global/filesystem.h>
#include <global/name.h>
#include <global/simd/vector.h>
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
    static void OpenTimingFile(OutputFileStream& timingFile, Core::Alloc::ScratchArena& scratchArena){
        const auto timingPath = ReadSmokeEnvironmentText(scratchArena, "NWB_GPU_TIMING_FILE");
        if(!timingPath)
            return;

        timingFile.open(timingPath->c_str(), s_FileOpenAppend);
        if(!timingFile.is_open())
            NWB_LOGGER_ERROR(NWB_TEXT("GpuPassTimingProbe: failed to open requested timing output"));
    }


public:
    explicit GpuPassTimingProbe(TStringView label)
        : m_arena(s_Arena)
        , m_scopes(m_arena)
        , m_label(label)
    {}


public:
    void recordFrame(const f32 delta, const Core::Perf::TimingView& gpuTiming){
        const f64 safeDelta = IsFinite(delta) ? VectorDoubleGetX(VectorDoubleMax(VectorDoubleReplicate(static_cast<f64>(delta)), VectorDoubleZero())) : 0.0;
        if(safeDelta <= 0.0)
            return;

        const SIMDVectorDouble frameSeconds = VectorDoubleReplicate(safeDelta);
        m_elapsedSeconds = VectorDoubleGetX(VectorDoubleAdd(VectorDoubleReplicate(m_elapsedSeconds), frameSeconds));
        if(m_elapsedSeconds < s_WarmupSeconds)
            return;
        if(!gpuTiming.valid())
            return;

        m_intervalSeconds = VectorDoubleGetX(VectorDoubleAdd(VectorDoubleReplicate(m_intervalSeconds), frameSeconds));
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
            const SIMDVectorDouble windowSeconds = VectorDoubleReplicate(stats.seconds);
            if(accum.frames == 0u){
                accum.minSeconds = stats.seconds;
                accum.maxSeconds = stats.seconds;
            }
            else{
                const SIMDVectorDouble extremaSeconds = VectorDoubleSet(accum.minSeconds, accum.maxSeconds, 0.0, 0.0);
                const SIMDVectorDouble nextExtremaSeconds = VectorDoubleSelect(VectorDoubleMin(extremaSeconds, windowSeconds), VectorDoubleMax(extremaSeconds, windowSeconds), s_DoubleSIMDMaskY);
                accum.minSeconds = VectorDoubleGetX(nextExtremaSeconds);
                accum.maxSeconds = VectorDoubleGetY(nextExtremaSeconds);
            }
            accum.sumSeconds = VectorDoubleGetX(VectorDoubleAdd(VectorDoubleReplicate(accum.sumSeconds), windowSeconds));
            accum.samples += stats.sampleCount;
            ++accum.frames;
        }
    }

    void report(const Core::Perf::TimingView& gpuTiming){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("{}: gpu per-pass timing over {} frames / {}s"), m_label, m_intervalFrames, m_intervalSeconds);

        // GUI logging is not scrapeable; the optional file sink exposes per-pass A/B timings.
        Core::Alloc::ScratchArena scratchArena(s_SmokeEnvironmentArena);
        OutputFileStream timingFile;
        OpenTimingFile(timingFile, scratchArena);
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
            const SIMDVectorDouble averageSeconds = VectorDoubleDivide(VectorDoubleReplicate(accum.sumSeconds), VectorDoubleReplicate(static_cast<f64>(accum.frames)));
            // Lanes are window average, min, max and total.
            const SIMDVectorDouble windowMs = VectorDoubleScale(VectorDoubleSelect(VectorDoubleSet(0.0, accum.minSeconds, accum.maxSeconds, accum.sumSeconds), averageSeconds, s_DoubleSIMDMaskX), s_MillisecondsPerSecond);
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("  {}: gpu_window_ms avg={} min={} max={} published_windows={}"), StringConvert(scopeName.resolvedText()), VectorDoubleGetX(windowMs), VectorDoubleGetY(windowMs), VectorDoubleGetZ(windowMs), accum.frames);
            if(timingFile.is_open()){
                timingFile
                    << "  " << scopeName.resolvedText()
                    << ": window_avg_ms=" << VectorDoubleGetX(windowMs)
                    << " window_min_ms=" << VectorDoubleGetY(windowMs)
                    << " window_max_ms=" << VectorDoubleGetZ(windowMs)
                    << " published_windows=" << static_cast<unsigned>(accum.frames)
                    << " total_ms=" << VectorDoubleGetW(windowMs)
                    << " gpu_samples=" << accum.samples
                    << " sample_avg_ms=" << VectorDoubleGetX(VectorDoubleDivide(VectorDoubleSplatW(windowMs), VectorDoubleReplicate(static_cast<f64>(accum.samples))))
                    << '\n'
                ;
            }
        }
        if(timingFile.is_open()){
            timingFile.close();
            if(!timingFile.good())
                NWB_LOGGER_ERROR(NWB_TEXT("GpuPassTimingProbe: timing output write or close failed"));
        }
    }

    void resetInterval()noexcept{
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

